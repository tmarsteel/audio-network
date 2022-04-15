#include <FreeRTOS.h>
#include <Arduino.h>
#include "network.hpp"
#include "runtime.hpp"
#include "config.hpp"
#include <math.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_interface.h>
#include <esp_event_loop.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <lwip/dhcp.h>
#include <lwip/udp.h>
#include <AsyncUDP.h>

static const char* LOGTAG = "network";

static EventGroupHandle_t network_event_group;

#ifdef LOG_LOCAL_LEVEL
#undef LOG_LOCAL_LEVEL
#endif
#define LOG_LOCAL_LEVEL ESP_LOG_INFO

ip4_addr_t network_get_broadcast_address(ip4_addr_t* sample_ip_in_network, ip4_addr_t* netmask) {
    uint32_t network = sample_ip_in_network->addr & netmask->addr;
    uint32_t boradcast_addr = network | (~netmask->addr);
    return {
        .addr = boradcast_addr
    };
}

wifi_config_t network_await_config() {    
    config_wifi_t wifi_config;
    esp_err_t err;
    do {
        err = config_await_and_get_wifi(&wifi_config, portMAX_DELAY);
    } while (err == ESP_ERR_TIMEOUT);
    ESP_ERROR_CHECK(err);
    
    size_t ssid_length = strlen(wifi_config.ssid);
    if (ssid_length > 33) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    size_t psk_length = strlen(wifi_config.psk);
    if (psk_length > 64) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
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
    esp_wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    return esp_wifi_config;
}

static uint network_connect_retry_attempts = 0;
static esp_err_t network_esp_event_handler(void* ctx, system_event_t* event) {
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            return esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            Serial.printf("[network] Got IP-Address %s\n", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            xEventGroupSetBits(network_event_group, NETWORK_EVENT_GROUP_BIT_CONNECTED);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            {
                Serial.println("[network] Disconnected from AP");
                xEventGroupClearBits(network_event_group, NETWORK_EVENT_GROUP_BIT_CONNECTED);
                if (network_connect_retry_attempts < NETWORK_MAX_CONNECT_RETRY_ATTEMPTS) {
                    esp_err_t connect_error = esp_wifi_connect();
                    network_connect_retry_attempts++;
                    return connect_error;
                } 

                Serial.printf(
                    "[network] Could not reconnect after %d attempts. Trying again in %dms\n",
                    NETWORK_MAX_CONNECT_RETRY_ATTEMPTS,
                    NETWORK_RECONNECT_COOLDOWN_MILLIS
                );
                xEventGroupSetBits(network_event_group, NETWORK_EVENT_GROUP_BIT_RECONNECT_COOLDOWN);
                network_connect_retry_attempts = 0;
                break;
            }
        default:
            break;
    }

    return ESP_OK;
}

void network_task_reconnect_after_cooldown(void* pvParameters) {
    while (true) {
        EventBits_t networkEventBits = xEventGroupWaitBits(network_event_group, NETWORK_EVENT_GROUP_BIT_RECONNECT_COOLDOWN, pdFALSE, pdFALSE, portMAX_DELAY);
        if ((networkEventBits & NETWORK_EVENT_GROUP_BIT_RECONNECT_COOLDOWN) > 0) {
            vTaskDelay(NETWORK_RECONNECT_COOLDOWN_MILLIS * portTICK_RATE_MS);
            xEventGroupClearBits(network_event_group, NETWORK_EVENT_GROUP_BIT_RECONNECT_COOLDOWN);
            ESP_ERROR_CHECK(esp_wifi_connect());
        }
    }
}

void network_task_udp_boradcast_announcement(void* pvParameters) {
    while(true) {
        EventBits_t networkEventBits = xEventGroupWaitBits(network_event_group, NETWORK_EVENT_GROUP_BIT_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
        if ((networkEventBits & NETWORK_EVENT_GROUP_BIT_CONNECTED) > 0) {
            tcpip_adapter_ip_info_t ip_info;
            ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
            ip4_addr_t broadcas_address = network_get_broadcast_address(&ip_info.ip, &ip_info.netmask);
            Serial.printf("Broadcast IP: %s\n", ip4addr_ntoa(&broadcas_address));
            vTaskDelay(NETWORK_BROADCAST_ANNOUNCEMENT_INTERVAL_MILLIS * portTICK_RATE_MS);
        }
    }
}

void network_initialize() {
    network_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    Serial.println("TCP adapter initialized");

    ESP_ERROR_CHECK(esp_event_loop_init(network_esp_event_handler, NULL));

    wifi_config_t wifi_config = network_await_config();
    Serial.println("[network] Got WiFi data from config module");

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());    

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, mac));
    char* mac_as_string = format_hex(mac, 6);
    Serial.printf("[network] WiFi started with MAC Address %s\n", mac_as_string);
    free(mac_as_string);

    TaskHandle_t taskHandle;
    BaseType_t rtosResult;
    
    rtosResult = xTaskCreate(
        network_task_reconnect_after_cooldown,
        "network-reconnect",
        configMINIMAL_STACK_SIZE * 2,
        nullptr,
        tskIDLE_PRIORITY,
        &taskHandle
    );
    if (rtosResult != pdPASS)
    {
        Serial.println("[network] Failed to start network reconnect task: OOM");
        panic();
    }

    rtosResult = xTaskCreate(
        network_task_udp_boradcast_announcement,
        "network-announce",
        configMINIMAL_STACK_SIZE * 2,
        nullptr,
        tskIDLE_PRIORITY,
        &taskHandle
    );
    if (rtosResult != pdPASS)
    {
        Serial.println("[network] Failed to start network announce task: OOM");
        panic();
    }
}