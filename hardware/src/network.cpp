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
#include <lwip/sockets.h>
#include <AsyncUDP.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "protogen/messages.pb.h"

static const char* LOGTAG = "network";

static EventGroupHandle_t network_event_group = nullptr;

#ifdef LOG_LOCAL_LEVEL
#undef LOG_LOCAL_LEVEL
#endif
#define LOG_LOCAL_LEVEL ESP_LOG_INFO

#define LWIP_ERROR_CHECK(x) lwip_error_check(x, __FILE__, __LINE__)
void lwip_error_check(err_t err_rc, const char* file, int line) {
    if (err_rc != ERR_OK) {
        Serial.printf("LWIP Error in %s on line %d: %s\n", file, line, lwip_strerr(err_rc));
        abort();
    }
}

ip4_addr_t network_get_broadcast_address(ip4_addr_t* sample_ip_in_network, ip4_addr_t* netmask) {
    uint32_t network = sample_ip_in_network->addr & netmask->addr;
    uint32_t boradcast_addr = network | (~netmask->addr);
    return  {
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

bool network_pb_encode_device_name(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    if (!pb_encode_tag_for_field(stream, field)) {
        return false;
    }

    static const char* deviceName = "Audio-Network Receiver";

    return pb_encode_string(stream, (pb_byte_t*) deviceName, strlen(deviceName));
}

AudioReceiverAnnouncement network_initialize_announcement() {
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, mac));

    uint64_t macAsLong = 0;
    for (int i = 0;i < 6;i++) {
        macAsLong |= ((uint64_t) mac[i]) << (i * 8);
    }

    AudioReceiverAnnouncement announcement = AudioReceiverAnnouncement_init_zero;
    announcement.magic_word = 0x2C5DA044;
    announcement.mac_address = macAsLong;
    announcement.currently_streaming = false;
    announcement.device_name.funcs.encode = network_pb_encode_device_name;

    return announcement;
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
    pb_byte_t* protobuf_buffer = (pb_byte_t*) malloc(512);
    int broadcast_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (broadcast_socket < 0) {
        Serial.println("Failed to create a socket for publishing announcements");
        abort();
    }


    // LWIP_ERROR_CHECK(udp_bind(udp_pcb, IP_ADDR_ANY, 0));

    AudioReceiverAnnouncement announcement = network_initialize_announcement();
    
    while(true) {
        EventBits_t networkEventBits = xEventGroupWaitBits(network_event_group, NETWORK_EVENT_GROUP_BIT_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
        if ((networkEventBits & NETWORK_EVENT_GROUP_BIT_CONNECTED) > 0) {
            pb_ostream_t pb_stream = pb_ostream_from_buffer(protobuf_buffer, 512);
            if (!pb_encode(&pb_stream, AudioReceiverAnnouncement_fields, &announcement)) {
                Serial.println("Buffer too small to hold announcement message.");
                abort();
            }

            tcpip_adapter_ip_info_t ip_info;
            ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
            ip4_addr_t broadcast_address_4 = network_get_broadcast_address(&ip_info.ip, &ip_info.netmask);
            sockaddr_in broadcast_socket_address;
            broadcast_socket_address.sin_addr.s_addr = broadcast_address_4.addr;
            broadcast_socket_address.sin_family = AF_INET;
            broadcast_socket_address.sin_port = htons(NETWORK_PORT_ANNOUNCE);

            int err = sendto(broadcast_socket, protobuf_buffer, pb_stream.bytes_written, 0, (struct sockaddr*) &broadcast_socket_address, sizeof(broadcast_socket_address));
            if (err < 0) {
                Serial.printf("Failed to send announcement: errno %d\n", errno);
            } else {
                Serial.printf("Sent broadcast to %s\n", ip4addr_ntoa(&broadcast_address_4));
            }
            
            vTaskDelay(NETWORK_ANNOUNCEMENT_INTERVAL_MILLIS * portTICK_RATE_MS);
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
        "net-reconnect",
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
        "net-announce",
        configMINIMAL_STACK_SIZE * 10,
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

network_state_t network_get_state() {
    if (network_event_group == nullptr) {
        return network_state_t::NETWORK_STATE_DISCONNECTED;
    }

    EventBits_t stateBits = xEventGroupGetBits(network_event_group);
    if ((stateBits & NETWORK_EVENT_GROUP_BIT_ACTIVE_STREAM) > 0) {
        return network_state_t::NETWORK_STATE_STREAM_INCOMING;
    }

    if ((stateBits & NETWORK_EVENT_GROUP_BIT_CONNECTED)) {
        return network_state_t::NETWORK_STATE_CONNECTED;
    }

    return network_state_t::NETWORK_STATE_DISCONNECTED;
}