/* ================================================================
 * display.h — 7-segment display rendering
 *
 * Pure rendering functions: given the values to show and the
 * display objects to show them on, these draw and nothing else.
 * They know nothing about game state, presets, or EEPROM.
 *
 * Created by Helder da Rocha on 11/07/26.
 * ================================================================ */

// #ifndef CHESS_CLOCKS_ARDUINO_DISPLAY_H
// #define CHESS_CLOCKS_ARDUINO_DISPLAY_H
// #endif //CHESS_CLOCKS_ARDUINO_DISPLAY_H
#pragma once

#include <Arduino.h>
#include <Adafruit_LEDBackpack.h>

// Renders remaining time onto one display. Automatically switches between
// MM:SS (below 100 minutes) and H:MM (100 minutes and up: one hour digit,
// two-digit minutes, seconds dropped) using a single hard threshold with
// no hysteresis.
void updateDisplay(Adafruit_7segment &d, unsigned long ms);

// Same rendering as updateDisplay(), but for the continuously-ticking
// countdown during RUNNING. Applies hysteresis around the MM:SS/H:MM
// cutover (enters H:MM at >=100:00, only drops back to MM:SS below 98:00)
// so a bonus repeatedly nudging the value across the boundary doesn't
// flicker the format every turn.
// wasHourFormat - the mode returned by the previous call for this display
// (or false, initially); the caller must store the returned value and
// pass it back in next time.
bool updateRunningDisplay(Adafruit_7segment &d, unsigned long ms, bool wasHourFormat);

// Shows a two-digit preset index on d1 and that preset's time (in ms) on d2.
void showPresetSelect(uint8_t idx, unsigned long presetMs,
                      Adafruit_7segment &d1, Adafruit_7segment &d2);

// Shows both players' remaining time (used for PAUSED and similar states).
void showPausedDisplays(Adafruit_7segment &d1, unsigned long time1,
                        Adafruit_7segment &d2, unsigned long time2);

// Shows the player currently being edited during TIMESET; blanks the other
// display. stage == 0 edits/shows player 1 (on d1); any other value edits/
// shows player 2 (on d2).
void showTimeSetDisplay(uint8_t stage,
                        Adafruit_7segment &d1, unsigned long time1,
                        Adafruit_7segment &d2, unsigned long time2);
