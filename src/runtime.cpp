#include <Arduino.h>
#include <runtime.hpp>
#include <pins.hpp>

void panic() {
  pinMode(PIN_LED, OUTPUT);

  while(true) {
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