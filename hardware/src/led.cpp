#include <Arduino.h>
#include "led.hpp"
#include <math.h>
#include "pins.hpp"
#include "runtime.hpp"
#include "config.hpp"
#include "network.hpp"

struct led_phase_t {
    boolean red;
    boolean green;
    boolean blue;
    int duration_millis;
};

static led_phase_t led_appearance_disconnected[2] = {
    {
        .red = true,
        .green = false,
        .blue = false,
        .duration_millis = 500
    },
    {
        .red = false,
        .green = false,
        .blue = false,
        .duration_millis = 500
    }
};

static led_phase_t led_appearance_connected[1] = {
    {
        .red = false,
        .green = true,
        .blue = false,
        .duration_millis = 1000
    }
};

static led_phase_t led_appearance_config[2] = {
    {
        .red = false,
        .green = false,
        .blue = true,
        .duration_millis = 500
    },
    {
        .red = false,
        .green = false,
        .blue = false,
        .duration_millis = 500
    }
};

void led_apply_state(led_phase_t* state) {
    if (state->red) {
        digitalWrite(PIN_LED_RED, LOW);
    } else {
        digitalWrite(PIN_LED_RED, HIGH);
    }

    if (state->green) {
        digitalWrite(PIN_LED_GREEN, LOW);
    } else {
        digitalWrite(PIN_LED_GREEN, HIGH);
    }

    if (state->blue) {
        digitalWrite(PIN_LED_BLUE, LOW);
    } else {
        digitalWrite(PIN_LED_BLUE, HIGH);
    }
}

void led_get_appearance(led_phase_t** pxPhases, size_t* pxLen) {
    if (config_is_interface_active()) {
        *pxPhases = &led_appearance_config[0];
        *pxLen = 2;
        return;
    }

    network_state_t network_state = network_get_state();
    switch (network_state) {
        case NETWORK_STATE_CONNECTED:
        case NETWORK_STATE_STREAM_INCOMING:
            *pxPhases = &led_appearance_connected[0];
            *pxLen = 1;
            break;
        case NETWORK_STATE_DISCONNECTED:
            *pxPhases = &led_appearance_disconnected[0];
            *pxLen = 2;
            break;
        default:
            Serial.println("Unknown device state, cannot determine LED appearance");
            panic();
    }
}

void led_indicator_task(void* pvParameters) {
    led_phase_t* current_phases = &led_appearance_disconnected[0];
    size_t current_phases_len = sizeof(led_appearance_disconnected) / sizeof(led_phase_t);
    led_get_appearance(&current_phases, &current_phases_len);

    size_t current_phase_offset = 0;
    unsigned long current_phase_started_at = millis();

    led_apply_state(&current_phases[0]);

    while (true) {
        led_phase_t* next_phases;
        size_t next_phases_len;
        led_get_appearance(&next_phases, &next_phases_len);

        if (current_phases != next_phases) {
            current_phases = next_phases;
            current_phases_len = next_phases_len;
            current_phase_offset = 0;
            current_phase_started_at = millis();

            led_apply_state(&current_phases[0]);
            continue;
        }

        led_phase_t current_phase = current_phases[current_phase_offset];

        int passed_in_current_phase = (int) (millis() - current_phase_started_at);
        if (passed_in_current_phase < current_phase.duration_millis) {
            size_t delay_millis = min(LED_MAX_REACTION_TIME_MILLIS, current_phase.duration_millis - passed_in_current_phase);
            vTaskDelay(delay_millis / portTICK_PERIOD_MS);
            continue;
        }

        int overshoot_duration = max(0, passed_in_current_phase - current_phase.duration_millis);
        current_phase_offset++;
        if (current_phase_offset >= current_phases_len) {
            current_phase_offset = 0;
        }
        current_phase = current_phases[current_phase_offset];
        led_apply_state(&current_phase);
        
        current_phase_started_at = millis() - overshoot_duration;
    }
}

void led_initialize() {
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_BLUE, OUTPUT);
    digitalWrite(PIN_LED_RED, HIGH);
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_BLUE, HIGH);
    TaskHandle_t taskHandle;
    BaseType_t rtosResult;
    rtosResult = xTaskCreate(
        led_indicator_task,
        LED_TASK_NAME,
        configMINIMAL_STACK_SIZE * 3,
        nullptr,
        tskIDLE_PRIORITY,
        &taskHandle
    );
    if (rtosResult != pdPASS) {
        Serial.println("Failed to start the led indicator task");
        panic();
    }
}