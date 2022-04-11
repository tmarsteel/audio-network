#include <Arduino.h>
#include "driver/i2s.h"
#include "runtime.hpp"
#include "audio.hpp"
#include "pins.hpp"
#include "stdint.h"
#include "led.hpp"
#include "FreeRTOS.h"
#include "config.hpp"
#include "network.hpp"

#define SAMPLES_PER_SECOND 44100

static const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLES_PER_SECOND,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = true};

static const i2s_pin_config_t pin_config = {
    .bck_io_num = PIN_I2S_BASE_CLOCK,
    .ws_io_num = PIN_I2S_WORD_SELECT,
    .data_out_num = PIN_I2S_DATA_OUT,
    .data_in_num = I2S_PIN_NO_CHANGE};

void mute()
{
    pinMode(PIN_MUTE, OUTPUT);
    digitalWrite(PIN_MUTE, HIGH);
}

void unmute()
{
    pinMode(PIN_MUTE, OUTPUT);
    digitalWrite(PIN_MUTE, LOW);
}

void setup()
{
    mute();
    Serial.begin(50000);
    esp_err_t err;

    err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK)
    {
        Serial.printf("Failed to install i2s driver: %d\n", err);
        panic();
    }

    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK)
    {
        Serial.printf("Failed to set i2s pins: %d\n", err);
        panic();
    }

    err = i2s_set_sample_rates(I2S_NUM_0, SAMPLES_PER_SECOND);
    if (err != ESP_OK)
    {
        Serial.printf("Failed to set i2s sample rate: %d\n", err);
        panic();
    }

    Serial.println("i2s initialized successfully.");

    led_initialize();
    led_set_indicated_device_state(DEVICE_STATE_DISCONNECTED);

    config_initialize();
    network_initialize();
}

void loop()
{
    // the arduino compatibility layer launces a freertos task that
    // keeps on calling this one. We don't need it, as all of our code
    // runs via freertos, too.
    vTaskDelete(NULL);
}
