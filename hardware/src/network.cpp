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
#include "playback.hpp"
#include "audio.hpp"

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

#define SOCK_ERROR_CHECK(x) sock_error_check(x, __FILE__, __LINE__)
void sock_error_check(int result, const char* file, int line) {
    if (result < 0) {
        Serial.printf("Socket error in %s on line %d: errno %d\n", file, line, errno);
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

typedef struct {
    // set by network_pb_decode_audio_samples
    // that function will also set .len
    audio_buffer_t* target;
} pb_decode_audio_samples_ctx;

static const char* PB_ERRMSG_AUDIO_CHUNK_TOO_LARGE = "AudioData chunk too large (doesn't fit in a buffer from the playback module)";

bool network_pb_decode_audio_samples(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    audio_buffer_t* audio_buffer = playback_get_next_free_audio_buffer();
    if (stream->bytes_left > audio_buffer->capacity) {
        playback_hand_back_unused_buffer(audio_buffer);
        stream->errmsg = PB_ERRMSG_AUDIO_CHUNK_TOO_LARGE;
        return false;
    }

    pb_decode_audio_samples_ctx* context = (pb_decode_audio_samples_ctx*) (*arg);
    context->target = audio_buffer;
    context->target->len = stream->bytes_left;
    bool success = pb_read(stream, (pb_byte_t*) context->target->data, stream->bytes_left);
    if (!success) {
        context->target = nullptr;
        playback_hand_back_unused_buffer(audio_buffer);
    }
    return success;
}

bool network_pb_istream_from_socket_callback(pb_istream_t* stream, uint8_t* buffer, size_t count) {
    if (count == 0) {
        return true;
    }

    int socket = (int) (stream->state);
    uint8_t one_byte;
    if (buffer == NULL) {
        assert(false);
        int err = recv(socket, &one_byte, 1, 0);
        if (err == 0) {
            return count == 0;
        }
        if (err > 0) {
            count -= err;
        }

        return false;
    }

    int bytes_received_total = 0;
    do {
        int bytes_received = recv(socket, buffer + bytes_received_total, count - bytes_received_total, MSG_WAITALL);
        if (bytes_received < 0) {
            return false;
        }

        if (bytes_received == 0) {
            stream->bytes_left = 0;
            return false;
        }

        bytes_received_total += bytes_received;
    } while (bytes_received_total < count);

    return true;
}

pb_istream_t network_pb_istream_from_socket(int socket) {
    return pb_istream_s {
        .callback = network_pb_istream_from_socket_callback,
        .state = (void*) socket,
        .bytes_left = SIZE_MAX,
    };
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

void network_handle_next_client(int server_socket) {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = (socklen_t) sizeof(clientAddr);
    Serial.println("[network] waiting for a trasmitter to connect");
    int client_socket = accept(server_socket, (sockaddr*) &clientAddr, &clientAddrLen);
    Serial.printf("[network] transmitter %d connected, starting playback\n", clientAddr.sin_addr.s_addr);
    SOCK_ERROR_CHECK(client_socket);

    pb_istream_t pb_socket_stream = network_pb_istream_from_socket(client_socket);
    while (true) {
        pb_decode_audio_samples_ctx decode_context = {
            .target = nullptr
        };
        AudioData receivedData;
        receivedData.samples.funcs.decode = network_pb_decode_audio_samples;
        receivedData.samples.arg = &decode_context;
        if (!pb_decode_delimited(&pb_socket_stream, AudioData_fields, &receivedData)) {
            Serial.printf("Failed to read audio data protobuf (%s), closing connection.\n", pb_socket_stream.errmsg);
            shutdown(client_socket, 0);
            close(client_socket);
            return;
        }

        audio_buffer_t* audio_buffer = decode_context.target;
        assert(audio_buffer != nullptr);
        assert(receivedData.bytes_per_sample == 2); // TODO: implement proper error feedback to the transmitter
        audio_buffer->samples_per_channel_and_second = receivedData.samples_per_channel_and_second;
        adjust_volume_16bit_dual_channel((int16_t*) audio_buffer->data, audio_buffer->len / sizeof(int16_t) / 2, 0.25);
        
        if (audio_buffer->len == 0) {
            playback_hand_back_unused_buffer(audio_buffer);
        } else {
            playback_queue_audio(audio_buffer);
        }
    }
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
            }
            
            vTaskDelay(NETWORK_ANNOUNCEMENT_INTERVAL_MILLIS * portTICK_RATE_MS);
        }
    }
}

void network_task_accept_audio_stream(void* pvParameters) {
    sockaddr_in listen_sock_addr;
    listen_sock_addr.sin_addr.s_addr = IP_ADDR_ANY->u_addr.ip4.addr;
    listen_sock_addr.sin_family = AF_INET;
    listen_sock_addr.sin_port = htons(NETWORK_PORT_AUDIO_RX);

    while(true) {
        EventBits_t networkEventBits = xEventGroupWaitBits(network_event_group, NETWORK_EVENT_GROUP_BIT_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
        if ((networkEventBits & NETWORK_EVENT_GROUP_BIT_CONNECTED) > 0) {

            int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            SOCK_ERROR_CHECK(server_socket);
            SOCK_ERROR_CHECK(bind(server_socket, (sockaddr*) &listen_sock_addr, sizeof(listen_sock_addr)));
            SOCK_ERROR_CHECK(listen(server_socket, 0));
            Serial.println("[network] listen socket started");
            while (true) {
                network_handle_next_client(server_socket);
            }
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
    
    rtosResult = xTaskCreatePinnedToCore(
        network_task_reconnect_after_cooldown,
        "net-reconnect",
        configMINIMAL_STACK_SIZE * 2,
        nullptr,
        tskIDLE_PRIORITY,
        &taskHandle,
        0
    );
    if (rtosResult != pdPASS)
    {
        Serial.println("[network] Failed to start network reconnect task: OOM");
        panic();
    }

    rtosResult = xTaskCreatePinnedToCore(
        network_task_udp_boradcast_announcement,
        "net-announce",
        configMINIMAL_STACK_SIZE * 10,
        nullptr,
        tskIDLE_PRIORITY,
        &taskHandle,
        0
    );
    if (rtosResult != pdPASS)
    {
        Serial.println("[network] Failed to start network announce task: OOM");
        panic();
    }

    rtosResult = xTaskCreatePinnedToCore(
        network_task_accept_audio_stream,
        "net-rx",
        configMINIMAL_STACK_SIZE * 10,
        nullptr,
        tskIDLE_PRIORITY,
        &taskHandle,
        0
    );
    if (rtosResult != pdPASS)
    {
        Serial.println("[network] Failed to start network rx task: OOM");
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