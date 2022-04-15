#include <Arduino.h>
#include <runtime.hpp>
#include <pins.hpp>

void panic()
{
    pinMode(PIN_LED, OUTPUT);

    while (true)
    {
        digitalWrite(PIN_LED, HIGH);
        delay(150);
        digitalWrite(PIN_LED, LOW);
        delay(150);
        digitalWrite(PIN_LED, HIGH);
        delay(150);
        digitalWrite(PIN_LED, LOW);
        delay(3000 - (150 * 3));
    }
}

static const char runtime_hex_chars[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

char* format_hex(uint8_t *data, size_t len)
{
    char *blob = (char *)malloc(sizeof(uint8_t) * len * 2 + 1);
    blob[len * 2] = 0;

    for (size_t i = 0; i < len; i++)
    {
        uint8_t value = data[i];
        blob[i * 2] = runtime_hex_chars[value >> 4];
        blob[i * 2 + 1] = runtime_hex_chars[value & 0x0F];
    }

    return blob;
}