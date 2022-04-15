#include <stdint.h>

void generate_sine_wave_16bit_dual_channel(int frequency, int sampleRate, int16_t* buffer, int sampleCount);
void adjust_volume_16bit_dual_channel(int16_t* buffer, int sampleCount, double volume);
void write_silence_16bit_dual_channel(int16_t* buffer, int sampleCount);