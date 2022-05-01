#include <freertos/FreeRTOS.h>
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
#include <esp_event.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <lwip/dhcp.h>
#include <lwip/sockets.h>
#include <AsyncUDP.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "protogen/ip.pb.h"
#include "playback.hpp"
#include <opus.h>

#define MAX_ENCODED_FRAME_SIZE 4096

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
size_t sock_error_check(size_t result, const char* file, int line) {
    if (result < 0) {
        Serial.printf("Socket error in %s on line %d: errno %d\n", file, line, errno);
        abort();
    }
    return result;
}

char* network_esp_ipaddr_ntoa(esp_ip4_addr* addr) {
    ip4_addr_t addr2;
    addr2.addr = addr->addr;
    return ip4addr_ntoa(&addr2);
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

bool network_scan_and_set_bssi_with_best_signal(wifi_sta_config_t* config) {
    char ssidForScanConfig[34];
    memcpy(&ssidForScanConfig, &config->ssid[0], 33 * sizeof(char));
    ssidForScanConfig[33] = 0;
    wifi_scan_config_t scan_config = {
        .ssid = (uint8_t*) &(ssidForScanConfig[0])
    };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    uint16_t n_aps_found;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&n_aps_found));
    if (n_aps_found <= 0) {
        Serial.println("Scan found no APs, cannot choose the one with best rssi :(");
        return false;
    }

    uint16_t n_aps_to_consider =  min(n_aps_found, (uint16_t) 10);
    wifi_ap_record_t* records = (wifi_ap_record_t*) malloc(sizeof(wifi_ap_record_t) * n_aps_to_consider);
    assert(records != nullptr);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&n_aps_to_consider, records));
    Serial.printf("WiFi Scan result (showing %d of %d)\n", n_aps_to_consider, n_aps_found);
    int8_t max_rssi = -127;
    uint8_t max_rssi_bssid[6];
    for (size_t i = 0;i < n_aps_to_consider;i++) {
        wifi_ap_record_t ap_record = records[i];
        char* bssid_str = format_hex(&ap_record.bssid[0], 6);
        char ssid_str[34];
        memcpy(&ssid_str, &ap_record.ssid[0], 33);
        ssid_str[33] = 0;
        Serial.printf("SSID=%s (BSSID %s) at %ddbm\n", &ssid_str[0], bssid_str, ap_record.rssi);
        free(bssid_str);

        if (ap_record.rssi > max_rssi) {
            max_rssi = ap_record.rssi;
            memcpy(&max_rssi_bssid[0], &ap_record.bssid[0], 6 * sizeof(uint8_t));
        }
    }
    free(records);

    memcpy(&config->bssid[0], &max_rssi_bssid[0], 6 * sizeof(uint8_t));
    config->bssid_set = true;

    char* bssid_str = format_hex(&max_rssi_bssid[0], 6 * sizeof(uint8_t));
    Serial.printf("Chose BSSID = %s with RSSI %ddbm\n", bssid_str, max_rssi);
    free(bssid_str);

    return true;
}

static uint network_connect_retry_attempts = 0;
static void network_esp_event_handler(void* ctx, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifi_config_t wifi_config;
        ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));

        network_scan_and_set_bssi_with_best_signal(&wifi_config.sta);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

        esp_wifi_connect();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        Serial.printf(
            "[network] Got Network config; IP=" IPSTR ", Netmask=" IPSTR ", Default Gateway=" IPSTR "\n",
            IP2STR(&event->ip_info.ip),
            IP2STR(&event->ip_info.netmask),
            IP2STR(&event->ip_info.gw)
        );
        xEventGroupSetBits(network_event_group, NETWORK_EVENT_GROUP_BIT_CONNECTED);
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        Serial.println("[network] Disconnected from AP");
        xEventGroupClearBits(network_event_group, NETWORK_EVENT_GROUP_BIT_CONNECTED);
        if (network_connect_retry_attempts < NETWORK_MAX_CONNECT_RETRY_ATTEMPTS) {
            esp_err_t connect_error = esp_wifi_connect();
            network_connect_retry_attempts++;
            return;
        } 

        Serial.printf(
            "[network] Could not reconnect after %d attempts. Trying again in %dms\n",
            NETWORK_MAX_CONNECT_RETRY_ATTEMPTS,
            NETWORK_RECONNECT_COOLDOWN_MILLIS
        );
        xEventGroupSetBits(network_event_group, NETWORK_EVENT_GROUP_BIT_RECONNECT_COOLDOWN);
        network_connect_retry_attempts = 0;
        return;
    }
}

bool network_pb_encode_device_name(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    if (!pb_encode_tag_for_field(stream, field)) {
        return false;
    }

    static const char* deviceName = "Audio-Network Receiver";

    return pb_encode_string(stream, (pb_byte_t*) deviceName, strlen(deviceName));
}

typedef struct {
    void* data;
    size_t len;
} pb_bytes_ctxt;

static const char* PB_ERRMSG_OOM = "Out of memory";
static const char* PB_ERRMSG_MAX_ENCODED_FRAME_SIZE_EXCEEDED = "Encoded frame exceeds max size";

bool network_pb_callback_audio_data(pb_istream_t *istream, pb_ostream_t *ostream, const pb_field_t *field) {
    assert(istream != nullptr && ostream == nullptr);
    if (field->tag == AudioData_opus_encoded_frame_tag) {
        if (istream->bytes_left > MAX_ENCODED_FRAME_SIZE) {
            istream->errmsg = PB_ERRMSG_MAX_ENCODED_FRAME_SIZE_EXCEEDED;
            return false;
        }
        
        pb_bytes_ctxt* field_value = (pb_bytes_ctxt*) malloc(sizeof(pb_bytes_ctxt));
        if (field_value == nullptr) {
            istream->errmsg = PB_ERRMSG_OOM;
            return false;
        }
        field_value->len = istream->bytes_left;
        field_value->data = malloc(istream->bytes_left);
        if (field_value->data == nullptr) {
            istream->errmsg = PB_ERRMSG_OOM;
            return false;
        }
        
        if (!pb_read(istream, (pb_byte_t*) field_value->data, istream->bytes_left)) {
            return false;
        }
        
        ((AudioData*) field->message)->opus_encoded_frame.arg = field_value;
        return true;
    }

    return pb_default_field_callback(istream, ostream, field);
}

void network_pb_bytes_ctxt_destroy(pb_bytes_ctxt* ctxt) {
    free(ctxt->data);
    free(ctxt);
}

void network_pb_audio_data_destroy(AudioData* data) {
    if (data->opus_encoded_frame.arg != nullptr) {
        network_pb_bytes_ctxt_destroy((pb_bytes_ctxt*) data->opus_encoded_frame.arg);
    }
}

bool network_pb_istream_from_socket_callback(pb_istream_t* stream, uint8_t* buffer, size_t count) {
    if (count == 0) {
        return true;
    }

    int socket = (int) (stream->state);
    if (buffer == NULL) {
        uint8_t one_byte;
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

bool network_pb_ostream_from_socket_callback(pb_ostream_t* stream, const uint8_t* buf, size_t count) {
    int nBytesWrittenTotal = 0;
    while (nBytesWrittenTotal < count) {
        int nBytesWritten = write((int) stream->state, &buf[nBytesWrittenTotal], count - nBytesWrittenTotal);
        if (nBytesWritten == -1) {
            switch(errno) {
                case EBADF:
                    stream->errmsg = "Bad File Descriptor";
                    break;
                case EDESTADDRREQ:
                    stream->errmsg = "Destination address required";
                    break;
                case EFAULT:
                    stream->errmsg = "Segmentation fault";
                    break;
                case EINTR:
                    stream->errmsg = "Interrupted";
                    break;
                case EINVAL:
                    stream->errmsg = "input value";
                    break;
                case EIO:
                    stream->errmsg = "io error (EIO)";
                    break;
                case EPERM:
                    stream->errmsg = "Permission denied.";
                    break;
                default:
                    stream->errmsg = "Unknown";
                    break;
            }

            return false;
        }

        nBytesWrittenTotal += nBytesWritten;
    }

    return true;
}

pb_ostream_t network_pb_ostream_from_socket(int socket) {
    return pb_ostream_s {
        .callback = network_pb_ostream_from_socket_callback,
        .state = (void*) socket,
        .max_size = SIZE_MAX
    };
}

BroadcastMessage network_initialize_discovery_response() {
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));

    uint64_t macAsLong = 0;
    for (int i = 0;i < 6;i++) {
        macAsLong |= ((uint64_t) mac[i]) << (i * 8);
    }

    const char* opus_version_string = opus_get_version_string();
    size_t opus_version_string_len = strlen(opus_version_string);
    assert(opus_version_string_len < 128);

    BroadcastMessage broadcast = BroadcastMessage_init_zero;
    broadcast.magic_word = 0x2C5DA044;
    broadcast.which_message = BroadcastMessage_discovery_response_tag;
    broadcast.message.discovery_response.currently_streaming = false; // TODO: fill with actual value
    broadcast.message.discovery_response.mac_address = macAsLong;
    broadcast.message.discovery_response.protocol_version = 1;
    memcpy(broadcast.message.discovery_response.opus_version, opus_version_string, opus_version_string_len);

    return broadcast;
}

void network_handle_next_client(int server_socket) {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = (socklen_t) sizeof(clientAddr);
    Serial.println("[network] waiting for a trasmitter to connect");
    int client_socket = accept(server_socket, (sockaddr*) &clientAddr, &clientAddrLen);
    SOCK_ERROR_CHECK(client_socket);
    Serial.printf("[network] transmitter %d connected\n", clientAddr.sin_addr.s_addr);
    pb_ostream_t pb_socket_ostream = network_pb_ostream_from_socket(client_socket);
    {
        ToTransmitter helloMessage = ToTransmitter_init_zero;
        helloMessage.which_message = ToTransmitter_receiver_information_tag;
        helloMessage.message.receiver_information.discovery_data = network_initialize_discovery_response().message.discovery_response;
        helloMessage.message.receiver_information.max_encoded_frame_size = 4096;
        helloMessage.message.receiver_information.max_decoded_frame_size = playback_get_maximum_frame_size_bytes();
        if (!pb_encode_delimited(&pb_socket_ostream, ToTransmitter_fields, &helloMessage)) {
            const char* errMsg = pb_socket_ostream.errmsg;
            if (errMsg == nullptr) {
                errMsg = "Unknown";
            }
            Serial.printf("Failed to write hello message, closing connection: %s\n", errMsg);
            shutdown(client_socket, 0);
            close(client_socket);
            return;
        }
    }

    pb_istream_t pb_socket_istream = network_pb_istream_from_socket(client_socket);
    playback_start_new_stream();
    
    while (true) {
        ToReceiver toReceiver = ToReceiver_init_zero;
        if (!pb_decode_delimited(&pb_socket_istream, ToReceiver_fields, &toReceiver)) {
            const char* errMsg = pb_socket_istream.errmsg;
            if (errMsg == nullptr) {
                errMsg = "Unknown";
            }
            Serial.printf("Failed to read audio data protobuf (%s), closing connection.\n", errMsg);
            break;
        }

        if (toReceiver.which_message != ToReceiver_audio_data_tag) {
            Serial.println("unknown message, closing connection.");
            break;
        }

        pb_bytes_ctxt* audio_data = (pb_bytes_ctxt*) toReceiver.message.audio_data.opus_encoded_frame.arg;
        assert(audio_data != nullptr);

        ESP_ERROR_CHECK(playback_queue_audio(audio_data->data, audio_data->len));
        network_pb_audio_data_destroy(&toReceiver.message.audio_data);
    }

    shutdown(client_socket, 0);
    close(client_socket);
    return;
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

#define BROADCAST_MAGIC_WORD 0x2C5DA044
void network_task_discovery(void* pvParameters) {
    size_t protobuf_buffer_len = 768;
    pb_byte_t* protobuf_buffer = (pb_byte_t*) malloc(protobuf_buffer_len);
    int broadcast_socket = SOCK_ERROR_CHECK(socket(AF_INET, SOCK_DGRAM, IPPROTO_IP));
    {
        sockaddr_in listen_addr;
        listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        listen_addr.sin_family = AF_INET;
        listen_addr.sin_port = htons(NETWORK_PORT_DISCOVERY);
        SOCK_ERROR_CHECK(bind(broadcast_socket, (struct sockaddr*) &listen_addr, sizeof(listen_addr)));
    }

    BroadcastMessage discovery_response = network_initialize_discovery_response();
    
    while(true) {
        EventBits_t networkEventBits = xEventGroupWaitBits(network_event_group, NETWORK_EVENT_GROUP_BIT_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
        if ((networkEventBits & NETWORK_EVENT_GROUP_BIT_CONNECTED) == 0) {
            continue;
        }

        struct sockaddr_storage sender_addr;
        socklen_t sender_addr_len = sizeof(sender_addr);
        size_t n_bytes_received = SOCK_ERROR_CHECK(recvfrom(broadcast_socket, protobuf_buffer, protobuf_buffer_len, 0, (struct sockaddr *) &sender_addr, &sender_addr_len));

        BroadcastMessage received_message;
        pb_istream_t received_buffer_istream = pb_istream_from_buffer(protobuf_buffer, n_bytes_received);
        if (!pb_decode(&received_buffer_istream, BroadcastMessage_fields, &received_message)) {
            Serial.printf("Failed to decode broadcast message: %s\n", received_buffer_istream.errmsg);
            continue;
        }
        if (received_message.magic_word != BROADCAST_MAGIC_WORD) {
            continue;
        }
        if (received_message.which_message != BroadcastMessage_discovery_request_tag) {
            continue;
        }
            
        pb_ostream_t out_stream = pb_ostream_from_buffer(protobuf_buffer, protobuf_buffer_len);
        if (!pb_encode(&out_stream, BroadcastMessage_fields, &discovery_response)) {
            Serial.printf("Failed to encode discovery response: %s\n", out_stream.errmsg);
            abort();
        }

        SOCK_ERROR_CHECK(sendto(broadcast_socket, protobuf_buffer, out_stream.bytes_written, 0, (struct sockaddr *) &sender_addr, sizeof(sender_addr)));
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
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_config = network_await_config();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &network_esp_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &network_esp_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());   

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
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
        network_task_discovery,
        "net-announce",
        configMINIMAL_STACK_SIZE * 5,
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
        configMINIMAL_STACK_SIZE * 20,
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