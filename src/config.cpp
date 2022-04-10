#include <FreeRTOS.h>
#include "runtime.hpp"
#include <Arduino.h>
#include "pins.hpp"
#include "config.hpp"
#include "led.hpp"
#include <SPIFFS.h>

volatile static QueueHandle_t q_config_button_pressed;

void IRAM_ATTR interrupt_handler_config_button_pressed()
{
    // error irrelevant
    size_t dummy_data = 123;
    xQueueSendToBackFromISR(q_config_button_pressed, &dummy_data, NULL);
}

void config_task(void *pvParameters)
{
    while (true)
    {
        size_t trigger_payload;
        if (xQueueReceive(q_config_button_pressed, &trigger_payload, 0) == pdTRUE)
        {
            Serial.println("Entering config mode");
            led_set_indicated_device_state(device_state_t::DEVICE_STATE_CONFIG);
            // TODO: implement BLE TX+RX of config
        }
    }
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

    TaskHandle_t configTaskHandle;
    BaseType_t rtosResult = xTaskCreate(
        config_task,
        CONFIG_TASK_NAME,
        configMINIMAL_STACK_SIZE * 10,
        nullptr,
        tskIDLE_PRIORITY,
        &configTaskHandle);
    if (rtosResult != pdPASS)
    {
        Serial.println("Failed to start config interface: OOM");
        panic();
    }

    attachInterrupt(digitalPinToInterrupt(PIN_CONFIG_MODE_BUTTON), interrupt_handler_config_button_pressed, FALLING);
}