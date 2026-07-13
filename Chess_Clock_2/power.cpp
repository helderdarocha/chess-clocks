#include <Arduino.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include "power.h"
#include "config.h"    // DISPLAY_OFF, DISPLAY_ON (brightness levels)
#include "display.h"   // showPresetSelect(), showPausedDisplays()

// Tries to reduce battery use by turning off everything that is possible.
// This code is mostly boilerplate from the Arduino forum, with some modifications to
// work with the Adafruit 7-segment displays.
// See https://forum.arduino.cc/t/what-to-turn-off-and-how/79151/9
void goToSleep(Adafruit_7segment &d1, Adafruit_7segment &d2,
               bool &sleeping, bool &justWokeUp, unsigned long &lastActivityMs,
               bool showingPresetSelect,
               uint8_t presetIndex, unsigned long presetMs,
               unsigned long time1, unsigned long time2) {

  // Blank both displays
  d1.setBrightness(DISPLAY_OFF); d1.clear(); d1.writeDisplay();
  d2.setBrightness(DISPLAY_OFF); d2.clear(); d2.writeDisplay();
  sleeping = true;

  // Enable pin-change interrupts on all five buttons so any one wakes the CPU.
  // PLY1=4(PD4) PLY2=5(PD5) PAUSE=8(PB0) PLUS=12(PB4) MINUS=15(PC1)
  PCICR  |= _BV(PCIE0);   // enable PCINT[7:0]  (port B — PAUSE, PLUS)
  PCICR  |= _BV(PCIE1);   // enable PCINT[14:8] (port C — MINUS)
  PCICR  |= _BV(PCIE2);   // enable PCINT[23:16](port D — PLY1, PLY2)
  PCMSK0 |= _BV(PCINT0) | _BV(PCINT4);   // PB0=PAUSE, PB4=PLUS
  PCMSK1 |= _BV(PCINT9);                 // PC1=MINUS
  PCMSK2 |= _BV(PCINT20) | _BV(PCINT21); // PD4=PLY1, PD5=PLY2

  power_adc_disable();   // saves ~0.3mA
  power_spi_disable();   // not sure about this, but disabling it anyway
  power_usart0_disable(); // not sure about this, but disabling it anyway

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();              // halts here until interrupt fires

  // --- wake up here after any button press ---
  sleep_disable();

  // Disable pin-change interrupts again
  PCICR  &= ~(_BV(PCIE0) | _BV(PCIE1) | _BV(PCIE2));

  power_adc_enable(); // enable after waking up
  power_spi_enable();
  power_usart0_enable();

  // Restore displays
  d1.setBrightness(DISPLAY_ON);
  d2.setBrightness(DISPLAY_ON);
  sleeping = false;
  justWokeUp = true;
  lastActivityMs = millis();

  // Redraw whatever was showing before sleep
  if (showingPresetSelect) {
    showPresetSelect(presetIndex, presetMs, d1, d2);
  } else {
    showPausedDisplays(d1, time1, d2, time2);
  }
}

// Empty ISRs needed to wake the CPU
ISR(PCINT0_vect) {}
ISR(PCINT1_vect) {}
ISR(PCINT2_vect) {}