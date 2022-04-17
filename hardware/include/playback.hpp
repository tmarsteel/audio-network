#include <stddef.h>
#include <stdint.h>

typedef struct {
    void* data;
    size_t capacity;
    size_t len;
    uint32_t sampleRate;
} audio_buffer_t;

void playback_mute();
void playback_unmute();

/** must be called once for setup */
void playback_initialize();

/** returns the capacity of a single buffer obtained through playback_get_next_free_audio_buffer */
size_t playback_get_buffer_capacity();

/** waits indefinitely until a buffer becomes available. returns it */
audio_buffer_t* playback_get_next_free_audio_buffer();

/** puts the buffer into the queue for playback. waits indefinitely if the queue is full */
void playback_queue_audio(audio_buffer_t* buffer);

/** puts the buffer back into the free pool. Waits indefinitely if the queue is full. */
void playback_hand_back_unused_buffer(audio_buffer_t* buffer);