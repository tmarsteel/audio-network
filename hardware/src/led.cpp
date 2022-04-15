#include <Arduino.h>
#include "led.hpp"
#include <math.h>
#include "pins.hpp"
#include "runtime.hpp"

typedef enum {
    LED_STATE_ON = 0,
    LED_STATE_OFF = 1
} led_state;

struct led_phase_t {
    led_state state;
    int duration_millis;
};

static led_phase_t led_appearance_disconnected[2] = {
    {
        .state = LED_STATE_ON,
        .duration_millis = 800
    },
    {
        .state = LED_STATE_OFF,
        .duration_millis = 800
    }
};

static led_phase_t led_appearance_connected[1] = {
    {
        .state = LED_STATE_ON,
        .duration_millis = 1000
    }
};

static led_phase_t led_appearance_config[2] = {
    {
        .state = LED_STATE_ON,
        .duration_millis = 300
    },
    {
        .state = LED_STATE_OFF,
        .duration_millis = 300
    }
};

static device_state_t INDICATED_DEVICE_STATE = DEVICE_STATE_DISCONNECTED;
void led_set_indicated_device_state(device_state_t state) {
    INDICATED_DEVICE_STATE = state;
}

void led_apply_state(led_state state) {
    if (state == led_state::LED_STATE_ON) {
        digitalWrite(PIN_LED, HIGH);
    } else {
        digitalWrite(PIN_LED, LOW);
    }
}

void led_get_appearance(device_state_t state, led_phase_t** pxPhases, size_t* pxLen) {
    if (state == device_state_t::DEVICE_STATE_CONFIG) {
        *pxPhases = &led_appearance_config[0];
        *pxLen = 2;
    }
    else if (state == device_state_t::DEVICE_STATE_CONNECTED) {
        *pxPhases = &led_appearance_connected[0];
        *pxLen = 1;
    }
    else if (state == device_state_t::DEVICE_STATE_DISCONNECTED) {
        *pxPhases = &led_appearance_disconnected[0];
        *pxLen = 2;
    }
    else {
        Serial.printf("Unknown device state %d, cannot determine LED appearance\n", state);
        panic();
    }
}

void led_indicator_task(void* pvParameters) {
    device_state_t currently_indicating = DEVICE_STATE_DISCONNECTED;
    led_phase_t* current_phases = &led_appearance_disconnected[0];
    size_t current_phases_len = sizeof(led_appearance_disconnected) / sizeof(led_phase_t);
    led_get_appearance(currently_indicating, &current_phases, &current_phases_len);

    size_t current_phase_offset = 0;
    unsigned long current_phase_started_at = millis();

    led_apply_state(current_phases[0].state);

    while (true) {
        int passed_in_current_phase = (int) (millis() - current_phase_started_at);

        device_state_t state_now = INDICATED_DEVICE_STATE;
        if (state_now != currently_indicating) {
            currently_indicating = state_now;
            led_get_appearance(currently_indicating, &current_phases, &current_phases_len);
            size_t current_phase_offset = 0;
            unsigned long current_phase_started_at = millis();

            led_apply_state(current_phases[0].state);
            continue;
        }

        led_phase_t current_phase = current_phases[current_phase_offset];

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
        led_apply_state(current_phase.state);
        
        current_phase_started_at = millis() - overshoot_duration;
    }
}

void led_initialize() {
    pinMode(PIN_LED, OUTPUT);
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