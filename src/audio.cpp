#include <stdint.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <Arduino.h>

#define M_PI		3.14159265358979323846

void generate_sine_wave_16bit(int frequency, int sampleRate, double volume, int16_t* buffer, int offset, int sampleCount) {
    double radiansPerSecond = 2.0 * M_PI * frequency;
    double radiansPerSample = radiansPerSecond / ((double) sampleRate);
    for (int pos = 0;pos <= sampleCount;pos += 2) {
        double sampleAsDouble = sin(pos * radiansPerSample);
        int16_t sample = (int16_t) (sampleAsDouble * volume * 0x7FFF);
        buffer[offset + pos] = sample;
        buffer[offset + pos + 1] = 0;
    }
}