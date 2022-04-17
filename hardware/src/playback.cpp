#include "playback.hpp"
#include "pins.hpp"
#include "driver/i2s.h"
#include <FreeRTOS.h>
#include <Arduino.h>

volatile QueueHandle_t playback_empty_buffer_queue;
volatile QueueHandle_t playback_filled_buffer_queue;

#define AUDIO_BUFFER_BYTES_TOTAL (44100 / 8) * 2 * sizeof(uint16_t) // 125ms of audio at 44.1khz 16bit stereo
#define AUDIO_BUFFER_COUNT       2                                  // split accross two buffers

static const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 16,
    .dma_buf_len = 256,
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

esp_err_t playback_alloc_audio_buffer(size_t capacity, audio_buffer_t** bufferOut) {
    audio_buffer_t* control_struct = (audio_buffer_t*) malloc(sizeof(audio_buffer_t));
    if (control_struct == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    void* buffer = malloc(capacity);
    if (buffer == nullptr) {
        free(control_struct);
        return ESP_ERR_NO_MEM;
    }

    *control_struct = {
        .data = buffer,
        .capacity = capacity,
        .len = 0,
        // irrelevant, will be set/changed by the code that brings the audio data in
        .sampleRate = 44100 
    };

    *bufferOut = control_struct;
    return ESP_OK;
}

void playback_task_play_audio_from_buffers(void* pvParameters) {
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, &pin_config));
    ESP_ERROR_CHECK(i2s_stop(I2S_NUM_0));

    boolean within_playback = false;
    audio_buffer_t* current_buffer;
    size_t current_sample_rate = 0;
    size_t underflow_counter = 0;
    while (true) {
        BaseType_t got_buffer = xQueueReceive(playback_filled_buffer_queue, &current_buffer, 0);
        bool can_play_buffer = got_buffer == pdTRUE && current_buffer->len > 0;
        if (!can_play_buffer) {
            if (within_playback) {
                // TODO: underflow -> not enough network throughput? Notify audio source!
                within_playback = false;
                ESP_ERROR_CHECK(i2s_stop(I2S_NUM_0));
                underflow_counter++;
                if (underflow_counter % 100 == 0) {
                    Serial.printf("Underflow at %d\n", underflow_counter);
                }
            }
            
            got_buffer = xQueueReceive(playback_filled_buffer_queue, &current_buffer, portMAX_DELAY);
            if (got_buffer != pdTRUE || current_buffer->len == 0) {
                continue;
            }
        }

        if (current_sample_rate != current_buffer->sampleRate) {
            ESP_ERROR_CHECK(i2s_set_sample_rates(I2S_NUM_0, current_buffer->sampleRate));
            current_sample_rate = current_buffer->sampleRate;
        }
        
        if (!within_playback) {
            ESP_ERROR_CHECK(i2s_start(I2S_NUM_0));
            within_playback = true;
        }
        
        esp_err_t err;
        size_t bufferPos = 0;
        while (bufferPos < current_buffer->len) {
            size_t bytesWritten;
            err = i2s_write(I2S_NUM_0, (void*) (((char*) current_buffer->data) + bufferPos), current_buffer->len - bufferPos, &bytesWritten, portMAX_DELAY);
            if (err != ESP_OK) {
                Serial.printf("Failed to write bytes to the i2s DMA buffer: %d\n", err);
                abort();
            }
            bufferPos += bytesWritten;
        }
        current_buffer->len = 0;
        BaseType_t bufferWasPlaced = xQueueSend(playback_empty_buffer_queue, &current_buffer, 0);
        if (bufferWasPlaced != pdTRUE) {
            Serial.println("Was unable to place empty playback buffer pack into the pool. Something is messing with the queues internal to the playback module.");
            abort();
        }
    }
}

static size_t playback_buffer_capacity = 0;

void playback_initialize() {
    playback_empty_buffer_queue = xQueueCreate(AUDIO_BUFFER_COUNT, sizeof(audio_buffer_t*));
    playback_filled_buffer_queue = xQueueCreate(AUDIO_BUFFER_COUNT, sizeof(audio_buffer_t*));

    playback_buffer_capacity = AUDIO_BUFFER_BYTES_TOTAL / AUDIO_BUFFER_COUNT;
    for (int i = 0;i < AUDIO_BUFFER_COUNT;i++) {
        audio_buffer_t* playback_buffer;
        ESP_ERROR_CHECK(playback_alloc_audio_buffer(playback_buffer_capacity, &playback_buffer));
        if (xQueueSend(playback_empty_buffer_queue, &playback_buffer, 0) != pdTRUE) {
            Serial.printf("Failed to queue audio buffer %d; FreeRTOS queue misconfigured?\n", i);
            abort();
        }
    }
    
    Serial.printf("[playback] initialized buffers with %d bytes each\n", playback_buffer_capacity);

    TaskHandle_t taskHandle;
    BaseType_t rtosResult = xTaskCreatePinnedToCore(
        playback_task_play_audio_from_buffers,
        "playback",
        configMINIMAL_STACK_SIZE * 4,
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

audio_buffer_t* playback_get_next_free_audio_buffer() {
    audio_buffer_t* buffer;
    while (xQueueReceive(playback_empty_buffer_queue, &buffer, portMAX_DELAY) != pdTRUE);
    return buffer;
}

void playback_queue_audio(audio_buffer_t* buffer) {
    assert(buffer != nullptr);
    assert(buffer->len > 0);
    assert(buffer->len <= buffer->capacity);

    while (xQueueSend(playback_filled_buffer_queue, &buffer, portMAX_DELAY) != pdTRUE);
}

void playback_hand_back_unused_buffer(audio_buffer_t* buffer) {
    assert(buffer != nullptr);
    buffer->len = 0;
    while (xQueueSend(playback_empty_buffer_queue, &buffer, portMAX_DELAY) != pdTRUE);
}

size_t playback_get_buffer_capacity() {
    return playback_buffer_capacity;
}