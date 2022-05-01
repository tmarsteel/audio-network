#include <Arduino.h>
#include <runtime.hpp>
#include <pins.hpp>

void panic()
{
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_BLUE, OUTPUT);
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_BLUE, HIGH);

    while (true)
    {
        digitalWrite(PIN_LED_RED, LOW);
        delay(150);
        digitalWrite(PIN_LED_RED, HIGH);
        delay(150);
        digitalWrite(PIN_LED_RED, LOW);
        delay(150);
        digitalWrite(PIN_LED_RED, HIGH);
        delay(3000 - (150 * 3));
    }
}

static const char runtime_hex_chars[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

char* format_hex(const uint8_t *data, size_t len)
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

uint16_t to_uint16_exact(size_t value) {
    if (value > 0xFFFF) {
        abort();
    }

    return (uint16_t) value;
}