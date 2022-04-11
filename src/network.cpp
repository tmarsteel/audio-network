#include <FreeRTOS.h>
#include <esp_wifi.h>
#include <Arduino.h>
#include "runtime.hpp"
#include "config.hpp"
#include <math.h>

wifi_config_t network_await_config() {
    config_wifi_t wifi_config;
    esp_err_t err;
    do {
        err = config_await_and_get_wifi(&wifi_config, portMAX_DELAY);
    } while (err == ESP_ERR_TIMEOUT);
    ESP_ERROR_CHECK(err);
    
    size_t ssid_length = strlen(wifi_config.ssid);
    if (ssid_length > 33) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }
    size_t psk_length = strlen(wifi_config.psk);
    if (psk_length > 64) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }

    uint8_t ssid[32];
    for (int i = 0;i < 32;i++) {
        if (i < ssid_length) {
            ssid[i] = wifi_config.ssid[i];
        } else {
            ssid[i] = 0;
        }
    }
    uint8_t psk[64];
    for (int i = 0;i < 64;i++) {
        if (i < psk_length) {
            psk[i] = wifi_config.psk[i];
        } else {
            psk[i] = 0;
        }
    }

    wifi_config_t esp_wifi_config = {};
    memcpy(&(esp_wifi_config.sta.ssid), ssid, 32);
    memcpy(&(esp_wifi_config.sta.password), psk, 32);
    esp_wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    return esp_wifi_config;
}

void network_task(void* pvParameters) {
    wifi_config_t wifi_config = network_await_config();
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_AP, mac));
    char* mac_as_string = format_hex(mac, 6);
    Serial.printf("WiFi connected as MAC %s\n", mac_as_string);
    free(mac_as_string);

    while (true) {
        
    }
}

void network_initialize() {
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    TaskHandle_t configTaskHandle;
    BaseType_t rtosResult = xTaskCreate(
        network_task,
        CONFIG_TASK_NAME,
        configMINIMAL_STACK_SIZE * 10,
        nullptr,
        tskIDLE_PRIORITY,
        &configTaskHandle);
    if (rtosResult != pdPASS)
    {
        Serial.println("Failed to start network interface: OOM");
        panic();
    }
}