#include <lwip/ip4_addr.h>

#define NETWORK_EVENT_GROUP_BIT_CONNECTED          0b0001
#define NETWORK_EVENT_GROUP_BIT_RECONNECT_COOLDOWN 0b0010
#define NETWORK_EVENT_GROUP_BIT_ACTIVE_STREAM      0b0100

#define NETWORK_MAX_CONNECT_RETRY_ATTEMPTS             10
#define NETWORK_RECONNECT_COOLDOWN_MILLIS              1000

#define NETWORK_PORT_DISCOVERY  58765
#define NETWORK_PORT_AUDIO_RX   58764

/** must be called once for setup */
void network_initialize();

typedef enum {
    NETWORK_STATE_DISCONNECTED = 0,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_STREAM_INCOMING
} network_state_t;

network_state_t network_get_state();

ip4_addr_t network_get_broadcast_address(ip4_addr_t* sample_ip_in_network, ip4_addr_t* netmask);