#include <stdint.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <Arduino.h>

#define M_PI		3.14159265358979323846

void generate_sine_wave_16bit_dual_channel(int frequency, int sampleRate, double volume, int16_t* buffer, int offset, int sampleCount) {
    double radiansPerSecond = 2.0 * M_PI * frequency;
    double radiansPerSample = radiansPerSecond / ((double) sampleRate);
    for (int nSample = 0;nSample < sampleCount;nSample++) {
        double sampleAsDouble = sin(nSample * radiansPerSample);
        int16_t sample = (int16_t) (sampleAsDouble * volume * 0x7FFF);
        
        // dual channel
        buffer[offset + nSample * 2] = sample;
        buffer[offset + nSample * 2 + 1] = sample;
    }
}