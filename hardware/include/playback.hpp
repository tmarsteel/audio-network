#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

void playback_mute();
void playback_unmute();

/** must be called once for setup */
void playback_initialize();

/** returns the maximum number of bytes of decoded opus frames. */
size_t playback_get_maximum_frame_size_bytes();

/** copies the buffer into the queue for playback. waits indefinitely if the queue is full. */
esp_err_t playback_queue_audio(void* encoded_opus_frame, size_t len);

void playback_start_new_stream();