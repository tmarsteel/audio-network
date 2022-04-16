#include <Arduino.h>
#include "runtime.hpp"
#include "led.hpp"
#include "config.hpp"
#include "network.hpp"
#include "playback.hpp"
#include <esp_event_loop.h>

void setup()
{
    // playback_mute();
    Serial.begin(50000);
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    led_initialize();
    playback_initialize();
    config_initialize();
    network_initialize();
}

void loop()
{
    // the arduino compatibility layer launches a freertos task that
    // keeps on calling this one. We don't need it, as all of our code
    // runs via freertos, too.
    vTaskDelete(NULL);
}
