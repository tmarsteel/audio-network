#include <FreeRTOS.h>

#define CONFIG_TASK_NAME "config"

/** must be called once for setup */
void config_initialize();

/**
 * when called, makes the BLE config interface available
 * for a limited amount of time.
 */
void config_enable_from_ISR();