#include <stdint.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <Arduino.h>
#include <SPIFFS.h>

#define M_PI		3.14159265358979323846

void generate_sine_wave_16bit_dual_channel(int frequency, int sampleRate, int16_t* buffer, int sampleCount) {
    double radiansPerSecond = 2.0 * M_PI * frequency;
    double radiansPerSample = radiansPerSecond / ((double) sampleRate);
    for (int nSample = 0;nSample < sampleCount;nSample++) {
        double sampleAsDouble = sin(nSample * radiansPerSample);
        int16_t sample = (int16_t) (sampleAsDouble * 0x7FFF);

        // dual channel
        buffer[nSample * 2] = sample;
        buffer[nSample * 2 + 1] = sample;
    }
}

void adjust_volume_16bit_dual_channel(int16_t* buffer, int sampleCount, double volume) {
    for (int pos = 0;pos < sampleCount * 2;pos++) {
        buffer[pos] = (uint16_t) ((double) buffer[pos] * volume);
    }
}

void write_silence_16bit_dual_channel(int16_t* buffer, int sampleCount) {
    for (int pos = 0;pos < sampleCount * 2;pos++) {
        buffer[pos] = 0;
    }
}

size_t copy_spiffs_to_buffer_dual_channel(File fp, int16_t* buffer, size_t nSamples) {
  size_t nBytesToCopy = nSamples * sizeof(uint16_t) * 2;
  size_t nBytesCopied = 0;
  while (fp.available() && nBytesCopied < nBytesToCopy) {
    size_t nBytesCopiedNow = fp.readBytes((char*) &buffer[nBytesCopied], nBytesToCopy - nBytesCopied);
    nBytesCopied += nBytesCopiedNow;
  }

  return nBytesCopied;
}