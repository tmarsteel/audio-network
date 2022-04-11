#include <FreeRTOS.h>

#define CONFIG_TASK_NAME "config"

struct config_wifi_t {
    char* ssid;
    char* psk;

    void _free() {
        free(ssid);
        free(psk);
    }
};

/** must be called once for setup */
void config_initialize();

esp_err_t config_await_and_get_wifi(config_wifi_t* pxConfig, TickType_t xTicksToWait);