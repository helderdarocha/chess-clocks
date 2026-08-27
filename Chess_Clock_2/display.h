/* ================================================================
 * display.h — 7-segment display rendering
 *
 * Pure rendering functions: given the values to show and the
 * display objects to show them on, these draw and nothing else.
 * They know nothing about game state, presets, or EEPROM.
 * This implementation depends on the Adafruit_LEDBackpack library to
 * control two HT16K33 7-segment displays via I2C.
 *
 * Created by Helder da Rocha on 11/07/26.
 * ================================================================ */

// #ifndef CHESS_CLOCKS_ARDUINO_DISPLAY_H
// #define CHESS_CLOCKS_ARDUINO_DISPLAY_H
// #endif //CHESS_CLOCKS_ARDUINO_DISPLAY_H
#pragma once

#include <Arduino.h>
#include <Adafruit_LEDBackpack.h>

// Field indices for showPresetEditDisplay(), identifying which of the five
// digits of a custom preset is currently being edited.
#define PRESET_EDIT_HOURS      0
#define PRESET_EDIT_MIN_TENS   1
#define PRESET_EDIT_MIN_ONES   2
#define PRESET_EDIT_BONUS_TENS 3
#define PRESET_EDIT_BONUS_ONES 4

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

// Shows "5nd" on d1 and "0n"/"0ff" on d2 to report whether sound is
// currently enabled. Used when the user holds + or - for 3s while PAUSED.
// Purely draws once; the caller is responsible for how long it stays up
// and for redrawing whatever was on screen before.
void showSoundStatus(bool enabled, Adafruit_7segment &d1, Adafruit_7segment &d2);

// Shows the in-progress custom preset being built by the digit-by-digit
// editor (see PRESET_EDIT state in game.cpp): hours/minutes as H:MM on d1,
// bonus seconds in the last two digits of d2 (no colon, mirroring how the
// bonus is shown elsewhere as the "SS" half of a preset's MM:SS reading).
// `field` (one of the PRESET_EDIT_* constants above) is the digit currently
// being edited; when blinkVisible is false that one digit is blanked so the
// caller can flash it by toggling blinkVisible on a timer, while every other
// digit is always drawn normally. Purely draws once; the caller owns the
// blink timing and the eventual transition back to the preset-select screen.
void showPresetEditDisplay(uint8_t field, uint8_t hours, uint8_t minTens, uint8_t minOnes,
                           uint8_t bonusTens, uint8_t bonusOnes, bool blinkVisible,
                           Adafruit_7segment &d1, Adafruit_7segment &d2);

// Shows game statistics on d1/d2: total playing time so far (not counting
// paused time) and the current move number. Used when the user taps "-"
// while PAUSED. d1 shows the total time as H:MM once it reaches one hour,
// MM:SS below that (a lower threshold than updateDisplay()'s 100-minute
// cutover, since total game time is usually much shorter than a single
// player's remaining clock). d2 shows the move number right-justified,
// with a trailing decimal point on the last digit (chess-notation style,
// e.g. "40."), blanking unused leading digits. Purely draws once; the
// caller is responsible for how long it stays up and for redrawing
// whatever was on screen before.
void showGameStats(unsigned long totalPlayTimeMs, unsigned long moveCount,
                   Adafruit_7segment &d1, Adafruit_7segment &d2);