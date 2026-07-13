#include <Arduino.h>
#include "battery.h"
#include "config.h"
#include "sound.h"

// Only this module needs to remember when it last checked.
static unsigned long lastBattCheckMs = 0;

/*
 * This function measures the precise operating voltage (VCC) of an AVR microcontroller (Arduino)
 * without using any external components or pins. It works backwards by measuring a known
 * internal 1.1V reference voltage using VCC as the analog benchmark.
 */
long readVCC_mV() {
    // _BV is a bit value macro that shifts 1 to the left by the given number of bits.
    // ADMUX is the ADC Multiplexer Selection Register, which selects the input channel for the ADC.
    // REFS0 selects VCC as the reference voltage, and MUX3, MUX2, MUX1 select the internal 1.1V reference channel.
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
    delay(2);                      // let reference settle
    ADCSRA |= _BV(ADSC);           // start conversion (ADCSRA is the ADC Control and Status Register A)
    while (bit_is_set(ADCSRA, ADSC));  // wait till conversion is complete (ADSC bit is cleared when done)
    // VCC (mV) = 1100 * 1024 / ADC
    return 1125300L / ADC;     // 1100 * 1023 ≈ 1125300
}

// Three beeps, used both by the startup warning and the periodic check.
static void lowBatteryBeep() {
    for (int i = 0; i < 3; i++) {
        lowBatterySound();
        delay(220);
    }
}

void checkBatteryAtStartup(Adafruit_7segment &d1, Adafruit_7segment &d2) {
    if (readVCC_mV() >= BATT_WARN_MV) return;

    for (int i = 0; i < 2; i++) {
        // "BAtt" on display 1, "Lo--" on display 2
        d1.clear();
        d1.writeDigitRaw(0, 0x7C);  // b
        d1.writeDigitRaw(1, 0x77);  // A
        d1.writeDigitRaw(3, 0x78);  // t
        d1.writeDigitRaw(4, 0x78);  // t
        d1.drawColon(false);
        d1.writeDisplay();

        d2.clear();
        d2.writeDigitRaw(0, 0x38);  // L
        d2.writeDigitRaw(1, 0x5C);  // o
        d2.writeDigitRaw(3, 0x40);  // -
        d2.writeDigitRaw(4, 0x40);  // -
        d2.drawColon(false);
        d2.writeDisplay();

        lowBatteryBeep();           // beep while showing the warning

        // blank both displays between flashes
        d1.clear(); d1.writeDisplay();
        d2.clear(); d2.writeDisplay();
        delay(300);
    }
}

// Call periodically (e.g. once per loop) during RUNNING/PAUSED.
// See config.h to adjust BATT_CHECK_MS and BATT_WARN_MV.
void checkBattery() {
    unsigned long now = millis();
    if (now - lastBattCheckMs < BATT_CHECK_MS)
        return;
    lastBattCheckMs = now;
    if (readVCC_mV() < BATT_WARN_MV)
        lowBatteryBeep();
}
