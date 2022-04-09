#include <Arduino.h>
#include "driver/i2s.h"
#include "runtime.hpp"
#include "audio.hpp"
#include "pins.hpp"

#define SAMPLES_PER_SECOND 44100

static const i2s_config_t i2s_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLES_PER_SECOND,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
  .communication_format = I2S_COMM_FORMAT_I2S,
  .intr_alloc_flags = 0, // default interrupt priority
  .dma_buf_count = 2,
  .dma_buf_len = 64,
  .use_apll = true
};

static const i2s_pin_config_t pin_config = {
  .bck_io_num = PIN_I2S_BASE_CLOCK,
  .ws_io_num = PIN_I2S_WORD_SELECT,
  .data_out_num = PIN_I2S_DATA_OUT,
  .data_in_num = I2S_PIN_NO_CHANGE
};

void setup() {
  Serial.begin(50000);
  esp_err_t err;

  err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed to install i2s driver: %d\n", err);
    panic();
  }

  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed to set i2s pins: %d\n", err);
    panic();
  }

  err = i2s_set_sample_rates(I2S_NUM_0, SAMPLES_PER_SECOND);
  if (err != ESP_OK) {
    Serial.printf("Failed to set i2s sample rate: %d\n", err);
    panic();
  }

  Serial.println("i2s initialized successfully.");
}

void loop() {
  int bufferSampleCount = 1000;
  int bufferSize = sizeof(int16_t) * bufferSampleCount;
  int16_t* buffer = (int16_t*) malloc(bufferSize);
  
  if (buffer == nullptr) {
    Serial.println("Failed to create audio buffer: OOM");
    panic();
  }

  generate_sine_wave_16bit(440, SAMPLES_PER_SECOND, 0.05, &buffer[0], 0, bufferSampleCount);
  Serial.println("Sine wave written to buffer.");

  esp_err_t err;
  size_t bufferPos = 0;
  while (true) {
    size_t bytesWritten;
    err = i2s_write(I2S_NUM_0, &buffer[0] + bufferPos, bufferSize - bufferPos, &bytesWritten, portMAX_DELAY);
    if (err != ESP_OK) {
      Serial.printf("Failed to write bytes to the i2s DMA buffer: %d\n", err);
      panic();
    }
    bufferPos += bytesWritten;
    Serial.printf("Wrote %d bytes to the i2s buffer\n", bytesWritten);
    if (bufferPos >= sizeof(buffer)) {
      bufferPos = 0;
    }
  }
}