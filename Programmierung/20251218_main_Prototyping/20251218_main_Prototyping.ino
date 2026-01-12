/*
 * main.ino
 * FireBeetle ESP32 + THW-1060 (brushed ESC) + RS-540PH-7021
 * BLE UART (Adafruit Bluefruit compatible) + Control Pad + Serial debug
 *
 * Notes:
 * - ESC signal is standard servo: 50 Hz, 1000..2000 us (neutral ~1500 us).
 * - Forward-only policy by default (no reverse).
 */

#include <Arduino.h>

// Functions/Globals are implemented in functions.ino
void appSetup();
void appLoop();

void setup() {
  appSetup();
}

void loop() {
  appLoop();
}
