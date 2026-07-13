/* ================================================================
 * power.h — Sleep mode for power saving
 *
 * Blanks both displays, sleeps the CPU (SLEEP_MODE_PWR_DOWN) until any of
 * the five buttons wakes it via pin-change interrupt, then restores
 * brightness and redraws whatever was showing before sleep.
 *
 * Created by Helder da Rocha on 11/07/26.
 * ================================================================ */

//#ifndef CHESS_CLOCKS_ARDUINO_POWER_H
//#define CHESS_CLOCKS_ARDUINO_POWER_H
//#endif //CHESS_CLOCKS_ARDUINO_POWER_H
#pragma once

#include <Arduino.h>
#include <Adafruit_LEDBackpack.h>

// Call this from loop() when the clock has been idle for a while. It blanks
// both displays, puts the CPU to sleep, and wakes on any button press. After
// waking, it restores the brightness and redraws the display.
//
// The variables: `sleeping`, `justWokeUp`, and `lastActivityMs` are updated in place so the
// caller's copies stay in sync with what happened during the call.
//
// The variable `showingPresetSelect` selects what to redraw on waking:
// - pass `true` if the clock was in the preset-selection state (redraws `presetIndex` / `presetMs),
// - pass `false` otherwise (redraws both players' remaining time via `time1` / `time2`).
void goToSleep(Adafruit_7segment &d1, Adafruit_7segment &d2,
               bool &sleeping, bool &justWokeUp, unsigned long &lastActivityMs,
               bool showingPresetSelect,
               uint8_t presetIndex, unsigned long presetMs,
               unsigned long time1, unsigned long time2);