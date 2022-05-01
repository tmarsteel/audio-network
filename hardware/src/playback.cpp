#include "playback.hpp"
#include "pins.hpp"
#include "driver/i2s.h"
#include <freertos/FreeRTOS.h>
#include <Arduino.h>
#include <opus.h>
#include "runtime.hpp"

#define DECODE_AT_SAMPLE_RATE      48000
#define AUDIO_BUFFER_SIZE          (sizeof(opus_int16) * 48 * 60 * 2) // 60ms at 48khz stereo. This is the maximum according to the opus documentation
#define DMA_BUFFER_COUNT           8
#define DMA_BUFFER_SIZE            720
#define DMA_BUFFER_DURATION_MICROS (DMA_BUFFER_SIZE*DMA_BUFFER_COUNT / 48 / 2 / sizeof(opus_int16) * 1000)
#define DMA_BUFFER_DURATION_TICKS  (DMA_BUFFER_DURATION_MICROS / 1000 / portTICK_PERIOD_MS)

#define OPUS_ERROR_CHECK(x) opus_error_check(x, __FILE__, __LINE__)
void opus_error_check(int result, const char* file, int line) {
    if (result != OPUS_OK) {
        Serial.printf("Opus error in %s on line %d: %s (code %d)\n", file, line, opus_strerror(result), result);
        abort();
    }
}

typedef struct {
    void* data;
    size_t len;
} encoded_opus_frame_t;

static const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = DECODE_AT_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = DMA_BUFFER_COUNT,
    .dma_buf_len = DMA_BUFFER_SIZE,
    .use_apll = true};

static const i2s_pin_config_t pin_config = {
    .bck_io_num = PIN_I2S_BASE_CLOCK,
    .ws_io_num = PIN_I2S_WORD_SELECT,
    .data_out_num = PIN_I2S_DATA_OUT,
    .data_in_num = I2S_PIN_NO_CHANGE};

void playback_mute()
{
    pinMode(PIN_MUTE, OUTPUT);
    digitalWrite(PIN_MUTE, HIGH);
}

void playback_unmute()
{
    pinMode(PIN_MUTE, OUTPUT);
    digitalWrite(PIN_MUTE, LOW);
}

void playback_adjust_volume_16bit_dual_channel(double volume, opus_int16* buffer, size_t bufferLenBytes) {
    int sampleCount = bufferLenBytes / sizeof(opus_int16);
    for (int pos = 0;pos < sampleCount;pos++) {
        opus_int16 sample = buffer[pos];
        buffer[pos] = (opus_int16) ((double) sample * volume);
    }
}

static OpusDecoder* current_opus_decoder = nullptr;
void playback_start_new_stream() {
    if (current_opus_decoder != nullptr) {
        opus_decoder_destroy(current_opus_decoder);
    }
    int opus_error = OPUS_OK;
    current_opus_decoder = opus_decoder_create(DECODE_AT_SAMPLE_RATE, 2, &opus_error);
    OPUS_ERROR_CHECK(opus_error);
}

QueueHandle_t qEncodedOpusFrames;

opus_int16* decoded_audio_buffer;

void playback_task_play_audio_from_buffers(void* pvParameters) {
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, &pin_config));
    ESP_ERROR_CHECK(i2s_stop(I2S_NUM_0));

    boolean within_playback = false;
    size_t underflow_counter = 0;
    size_t decode_duration_ticks_avg = 0;
    while (true) {
        encoded_opus_frame_t* encoded_frame;
        BaseType_t got_buffer = xQueueReceive(qEncodedOpusFrames, &encoded_frame, max((unsigned int) 0, DMA_BUFFER_DURATION_TICKS - decode_duration_ticks_avg - 1)); // 10 ticks for decoding
        bool can_play_buffer = got_buffer == pdTRUE && encoded_frame->len > 0;
        if (!can_play_buffer) {
            if (within_playback) {
                // TODO: underflow -> not enough network throughput? Notify audio source!
                within_playback = false;
                ESP_ERROR_CHECK(i2s_stop(I2S_NUM_0));
                underflow_counter++;
                Serial.printf("Underflow at %d\n", underflow_counter);
                if (underflow_counter % 10 == 0) {
                    Serial.printf("AVG(decode_duration_ticks) = %d\n", decode_duration_ticks_avg);
                }
            }
            
            got_buffer = xQueueReceive(qEncodedOpusFrames, &encoded_frame, portMAX_DELAY);
            if (got_buffer != pdTRUE || encoded_frame->len == 0) {
                continue;
            }
        }

        if (!within_playback) {
            ESP_ERROR_CHECK(i2s_start(I2S_NUM_0));
            within_playback = true;
        }
        
        unsigned long decode_started_at = micros();
        size_t decoded_data_size = sizeof(opus_int16) * 2 * opus_packet_get_samples_per_frame((unsigned char*) encoded_frame->data, DECODE_AT_SAMPLE_RATE);
        assert(decoded_data_size <= AUDIO_BUFFER_SIZE);
        int nSamplesDecoded = opus_decode(current_opus_decoder, (unsigned char*) encoded_frame->data, encoded_frame->len, (opus_int16*) decoded_audio_buffer, AUDIO_BUFFER_SIZE, 0);
        if (nSamplesDecoded < 0) {
            OPUS_ERROR_CHECK(nSamplesDecoded);
        }
        decoded_data_size = nSamplesDecoded * 2 * sizeof(opus_int16);
        free(encoded_frame->data);
        free(encoded_frame);
        size_t decode_duration_ticks = (((size_t) (micros() - decode_started_at)) + (1000 * portTICK_PERIOD_MS) - 1) / (1000 * portTICK_PERIOD_MS);
        if (decode_duration_ticks_avg == 0) {
            decode_duration_ticks_avg = decode_duration_ticks;
        } else {
            decode_duration_ticks_avg = (decode_duration_ticks_avg + decode_duration_ticks) / 2;
        }

        esp_err_t err;
        int bufferPos = 0;
        while (bufferPos <decoded_data_size) {
            size_t bytesWritten;
            err = i2s_write(I2S_NUM_0, &(decoded_audio_buffer[bufferPos]), decoded_data_size - bufferPos, &bytesWritten, portMAX_DELAY);
            if (err != ESP_OK) {
                Serial.printf("Failed to write bytes to the i2s DMA buffer: %d\n", err);
                abort();
            }
            bufferPos += bytesWritten;
        }
    }
}

void playback_initialize() {
    decoded_audio_buffer = (opus_int16*) malloc(AUDIO_BUFFER_SIZE);
    if (decoded_audio_buffer == nullptr) {
        Serial.printf("OOM trying to allocate %d bytes of audio buffer\n", AUDIO_BUFFER_SIZE);
        abort();
    }
    qEncodedOpusFrames = xQueueCreate(40, sizeof(encoded_opus_frame_t*));
    
    playback_start_new_stream();

    TaskHandle_t taskHandle;
    BaseType_t rtosResult = xTaskCreatePinnedToCore(
        playback_task_play_audio_from_buffers,
        "playback",
        configMINIMAL_STACK_SIZE * 20,
        nullptr,
        7,
        &taskHandle,
        1
    );
    if (rtosResult != pdPASS)
    {
        Serial.println("[playback] Failed to start playback task: OOM");
        abort();
    }
    Serial.println("playback task started");
}

esp_err_t playback_queue_audio(void* encoded_opus_frame, size_t len) {
    encoded_opus_frame_t* encoded_frame = (encoded_opus_frame_t*) malloc(sizeof(encoded_opus_frame_t));
    if (encoded_frame == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    encoded_frame->data = malloc(len);
    encoded_frame->len = len;
    if (encoded_frame->data == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(encoded_frame->data, encoded_opus_frame, len);

    while (xQueueSend(qEncodedOpusFrames, &encoded_frame, portMAX_DELAY) != pdTRUE);

    return ESP_OK;
}

size_t playback_get_maximum_frame_size_bytes() {
    return AUDIO_BUFFER_SIZE;
}