/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.5 */

#ifndef PB_IP_PB_H_INCLUDED
#define PB_IP_PB_H_INCLUDED
#include <pb.h>

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Struct definitions */
typedef struct _AudioData { 
    pb_callback_t opus_encoded_frame; 
} AudioData;

typedef struct _DiscoveryResponse { 
    uint32_t protocol_version; 
    uint64_t mac_address; 
    char device_name[128]; 
    bool currently_streaming; 
    /* * the return value of opus_get_version_string() */
    char opus_version[128]; 
} DiscoveryResponse;

typedef struct _ReceiverError { 
    /* * there was no new audio data when playback of the previous frame finished */
    bool audio_underflow; 
    /* * when an AudioData packet could not be decoded */
    bool audio_decode_error; 
} ReceiverError;

/* *
 TCP port 58764 */
typedef struct _ToReceiver { 
    pb_size_t which_message;
    union {
        AudioData audio_data;
    } message; 
} ToReceiver;

/* *
 UDP port 58765 */
typedef struct _BroadcastMessage { 
    /* * must be set to 0x2C5DA044; to avoid clashes with other protocols on the same port */
    uint32_t magic_word; 
    /* * to the broadcast address */
    pb_size_t which_message;
    union {
        bool discovery_request;
        DiscoveryResponse discovery_response;
    } message; 
} BroadcastMessage;

typedef struct _ReceiverInformation { 
    DiscoveryResponse discovery_data; 
    /* * maximum number of bytes that AudioData.opus_encoded_frame can be */
    uint32_t max_encoded_frame_size; 
    /* * size of the decode buffer. Decoding happens at 48kHz 16bit stereo. */
    uint32_t max_decoded_frame_size; 
} ReceiverInformation;

/* *
 TCP port 58764 */
typedef struct _ToTransmitter { 
    pb_size_t which_message;
    union {
        ReceiverInformation receiver_information;
        ReceiverError error;
    } message; 
} ToTransmitter;


#ifdef __cplusplus
extern "C" {
#endif

/* Initializer values for message structs */
#define BroadcastMessage_init_default            {0, 0, {0}}
#define DiscoveryResponse_init_default           {0, 0, "", 0, ""}
#define ToReceiver_init_default                  {0, {AudioData_init_default}}
#define ToTransmitter_init_default               {0, {ReceiverInformation_init_default}}
#define ReceiverInformation_init_default         {DiscoveryResponse_init_default, 0, 0}
#define ReceiverError_init_default               {0, 0}
#define AudioData_init_default                   {{{NULL}, NULL}}
#define BroadcastMessage_init_zero               {0, 0, {0}}
#define DiscoveryResponse_init_zero              {0, 0, "", 0, ""}
#define ToReceiver_init_zero                     {0, {AudioData_init_zero}}
#define ToTransmitter_init_zero                  {0, {ReceiverInformation_init_zero}}
#define ReceiverInformation_init_zero            {DiscoveryResponse_init_zero, 0, 0}
#define ReceiverError_init_zero                  {0, 0}
#define AudioData_init_zero                      {{{NULL}, NULL}}

/* Field tags (for use in manual encoding/decoding) */
#define AudioData_opus_encoded_frame_tag         1
#define DiscoveryResponse_protocol_version_tag   1
#define DiscoveryResponse_mac_address_tag        2
#define DiscoveryResponse_device_name_tag        3
#define DiscoveryResponse_currently_streaming_tag 4
#define DiscoveryResponse_opus_version_tag       5
#define ReceiverError_audio_underflow_tag        1
#define ReceiverError_audio_decode_error_tag     2
#define ToReceiver_audio_data_tag                1
#define BroadcastMessage_magic_word_tag          1
#define BroadcastMessage_discovery_request_tag   2
#define BroadcastMessage_discovery_response_tag  3
#define ReceiverInformation_discovery_data_tag   1
#define ReceiverInformation_max_encoded_frame_size_tag 2
#define ReceiverInformation_max_decoded_frame_size_tag 3
#define ToTransmitter_receiver_information_tag   1
#define ToTransmitter_error_tag                  2

/* Struct field encoding specification for nanopb */
#define BroadcastMessage_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, UINT32,   magic_word,        1) \
X(a, STATIC,   ONEOF,    BOOL,     (message,discovery_request,message.discovery_request),   2) \
X(a, STATIC,   ONEOF,    MESSAGE,  (message,discovery_response,message.discovery_response),   3)
#define BroadcastMessage_CALLBACK NULL
#define BroadcastMessage_DEFAULT NULL
#define BroadcastMessage_message_discovery_response_MSGTYPE DiscoveryResponse

#define DiscoveryResponse_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, UINT32,   protocol_version,   1) \
X(a, STATIC,   REQUIRED, UINT64,   mac_address,       2) \
X(a, STATIC,   REQUIRED, STRING,   device_name,       3) \
X(a, STATIC,   REQUIRED, BOOL,     currently_streaming,   4) \
X(a, STATIC,   REQUIRED, STRING,   opus_version,      5)
#define DiscoveryResponse_CALLBACK NULL
#define DiscoveryResponse_DEFAULT NULL

#define ToReceiver_FIELDLIST(X, a) \
X(a, STATIC,   ONEOF,    MESSAGE,  (message,audio_data,message.audio_data),   1)
#define ToReceiver_CALLBACK NULL
#define ToReceiver_DEFAULT NULL
#define ToReceiver_message_audio_data_MSGTYPE AudioData

#define ToTransmitter_FIELDLIST(X, a) \
X(a, STATIC,   ONEOF,    MESSAGE,  (message,receiver_information,message.receiver_information),   1) \
X(a, STATIC,   ONEOF,    MESSAGE,  (message,error,message.error),   2)
#define ToTransmitter_CALLBACK NULL
#define ToTransmitter_DEFAULT NULL
#define ToTransmitter_message_receiver_information_MSGTYPE ReceiverInformation
#define ToTransmitter_message_error_MSGTYPE ReceiverError

#define ReceiverInformation_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, MESSAGE,  discovery_data,    1) \
X(a, STATIC,   REQUIRED, UINT32,   max_encoded_frame_size,   2) \
X(a, STATIC,   REQUIRED, UINT32,   max_decoded_frame_size,   3)
#define ReceiverInformation_CALLBACK NULL
#define ReceiverInformation_DEFAULT NULL
#define ReceiverInformation_discovery_data_MSGTYPE DiscoveryResponse

#define ReceiverError_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, BOOL,     audio_underflow,   1) \
X(a, STATIC,   REQUIRED, BOOL,     audio_decode_error,   2)
#define ReceiverError_CALLBACK NULL
#define ReceiverError_DEFAULT NULL

#define AudioData_FIELDLIST(X, a) \
X(a, CALLBACK, REQUIRED, BYTES,    opus_encoded_frame,   1)
#define AudioData_CALLBACK pb_default_field_callback
#define AudioData_DEFAULT NULL

extern const pb_msgdesc_t BroadcastMessage_msg;
extern const pb_msgdesc_t DiscoveryResponse_msg;
extern const pb_msgdesc_t ToReceiver_msg;
extern const pb_msgdesc_t ToTransmitter_msg;
extern const pb_msgdesc_t ReceiverInformation_msg;
extern const pb_msgdesc_t ReceiverError_msg;
extern const pb_msgdesc_t AudioData_msg;

/* Defines for backwards compatibility with code written before nanopb-0.4.0 */
#define BroadcastMessage_fields &BroadcastMessage_msg
#define DiscoveryResponse_fields &DiscoveryResponse_msg
#define ToReceiver_fields &ToReceiver_msg
#define ToTransmitter_fields &ToTransmitter_msg
#define ReceiverInformation_fields &ReceiverInformation_msg
#define ReceiverError_fields &ReceiverError_msg
#define AudioData_fields &AudioData_msg

/* Maximum encoded size of messages (where known) */
/* ToReceiver_size depends on runtime parameters */
/* AudioData_size depends on runtime parameters */
#define BroadcastMessage_size                    288
#define DiscoveryResponse_size                   279
#define ReceiverError_size                       4
#define ReceiverInformation_size                 294
#define ToTransmitter_size                       297

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
