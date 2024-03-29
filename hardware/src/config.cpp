#include <freertos/FreeRTOS.h>
#include "runtime.hpp"
#include <Arduino.h>
#include "pins.hpp"
#include "config.hpp"
#include "led.hpp"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <esp_err.h>

#define EVENTGROUP_BIT_WIFI_AVAILABLE 0b00000001
#define EVENTGROUP_BIT_CONFIG_ACTIVE  0b00000010

volatile static QueueHandle_t q_config_button_pressed;

void IRAM_ATTR interrupt_handler_config_button_pressed()
{
    // error irrelevant
    size_t dummy_data = 123;
    xQueueSendToBackFromISR(q_config_button_pressed, &dummy_data, NULL);
}

static EventGroupHandle_t config_event_group = nullptr;

void config_task(void *pvParameters)
{
    while (true)
    {
        size_t trigger_payload;
        if (xQueueReceive(q_config_button_pressed, &trigger_payload, 0) == pdTRUE)
        {
            Serial.println("Entering config mode");
            xEventGroupSetBits(config_event_group, EVENTGROUP_BIT_CONFIG_ACTIVE);
            // TODO: implement BLE TX+RX of config
        }
    }
}

boolean config_is_interface_active() {
    if (config_event_group == nullptr) {
        return false;
    }

    return (xEventGroupGetBits(config_event_group) & EVENTGROUP_BIT_CONFIG_ACTIVE) > 0;
}

void config_initialize()
{
    pinMode(PIN_CONFIG_MODE_BUTTON, INPUT_PULLUP);

    if (!SPIFFS.begin())
    {
        Serial.println("Failed to start SPIFFS");
        panic();
    }

    q_config_button_pressed = xQueueCreate(1, sizeof(size_t));
    if (q_config_button_pressed == nullptr)
    {
        Serial.println("Failed to create a queue for config button presses: OOM");
        panic();
    }

    config_event_group = xEventGroupCreate();

    TaskHandle_t configTaskHandle;
    BaseType_t rtosResult = xTaskCreatePinnedToCore(
        config_task,
        CONFIG_TASK_NAME,
        configMINIMAL_STACK_SIZE * 10,
        nullptr,
        tskIDLE_PRIORITY,
        &configTaskHandle,
        0
    );
    if (rtosResult != pdPASS)
    {
        Serial.println("Failed to start config interface: OOM");
        panic();
    }

    attachInterrupt(digitalPinToInterrupt(PIN_CONFIG_MODE_BUTTON), interrupt_handler_config_button_pressed, FALLING);
}

esp_err_t config_json_error_to_esp_error(DeserializationError err) {
    switch (err.code()) {
        case DeserializationError::Code::Ok:
            return ESP_OK;
        case DeserializationError::Code::NoMemory:
            return ESP_ERR_NO_MEM;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t config_copy_str_to_heap_nullterm(String* str, char** pxHeapAllocated) {
    if (str == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    unsigned int blobSize = sizeof(char) * str->length() + 1;
    char* blob = (char*) malloc(blobSize);
    if (blob == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    blob[str->length()] = 0;
    str->toCharArray(blob, blobSize);

    *pxHeapAllocated = blob;
    
    return ESP_OK;
}

esp_err_t config_await_and_get_wifi(config_wifi_t* pxConfig, TickType_t xTicksToWait)
{
    if ((xEventGroupGetBits(config_event_group) & EVENTGROUP_BIT_WIFI_AVAILABLE) == 0) {
        if (SPIFFS.exists("/config/wifi.json")) {
            xEventGroupSetBits(config_event_group, EVENTGROUP_BIT_WIFI_AVAILABLE);
        } else {
            EventBits_t bitsAfterWait = xEventGroupWaitBits(config_event_group, EVENTGROUP_BIT_WIFI_AVAILABLE, pdFALSE, pdFALSE, xTicksToWait);
            if ((bitsAfterWait & EVENTGROUP_BIT_WIFI_AVAILABLE) == 0) {
                return ESP_ERR_TIMEOUT;
            }
        }
    }
    
    File fp = SPIFFS.open("/config/wifi.json", "r");
    StaticJsonDocument<512> doc;
    
    ESP_ERROR_CHECK(config_json_error_to_esp_error(deserializeJson(doc, fp)));
    
    String ssid = doc["ssid"].as<String>();
    String psk = doc["psk"].as<String>();

    char* ssid_on_heap;
    ESP_ERROR_CHECK(config_copy_str_to_heap_nullterm(&ssid, &ssid_on_heap));
    char* psk_on_heap;
    ESP_ERROR_CHECK(config_copy_str_to_heap_nullterm(&psk, &psk_on_heap));

    pxConfig->ssid = ssid_on_heap;
    pxConfig->psk = psk_on_heap;

    return ESP_OK;
}