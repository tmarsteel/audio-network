#include <stdint.h>

void generate_sine_wave_16bit_dual_channel(int frequency, audio_buffer_t* target);
void adjust_volume_16bit_dual_channel(double volume, audio_buffer_t* buffer);
void write_silence_16bit_dual_channel(int16_t* buffer, int sampleCount);