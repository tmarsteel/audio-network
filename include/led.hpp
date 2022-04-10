#define LED_TASK_NAME                "led_indicator"
#define LED_MAX_REACTION_TIME_MILLIS 100

typedef enum {
    DEVICE_STATE_DISCONNECTED = 0,
    DEVICE_STATE_CONNECTED = 1,
    DEVICE_STATE_CONFIG = 2
} device_state_t;

void led_set_indicated_device_state(device_state_t state);

/** must be invoked once */
void led_initialize();