/* ================================================================
* sound.h — Buzzer sounds and the game-over melody
*
* Created by Helder da Rocha on 11/07/26.
* ================================================================ */

//#ifndef CHESS_CLOCKS_ARDUINO_SOUND_H
//#define CHESS_CLOCKS_ARDUINO_SOUND_H
//#endif //CHESS_CLOCKS_ARDUINO_SOUND_H
#pragma once

#include <Arduino.h>

// Loads the saved sound on/off preference from EEPROM (defaults to on if
// never saved). Call once from setup(), before anything else that might
// play a sound (e.g. checkBatteryAtStartup()).
void soundInit();

// True if sounds are currently enabled.
bool isSoundEnabled();

// Enables or disables all sounds and, if the value actually changed,
// persists the new preference to EEPROM. Safe to call every time (a
// no-op, including no EEPROM write, if the value isn't changing).
void setSoundEnabled(bool enabled);

// Short feedback tones. All of these (and gameOverTune() below) are
// silent no-ops while isSoundEnabled() is false.
void clickSound();      // preset/time-set +/- press
void modeSound();       // entering PAUSED from RUNNING, or SELECT_DURATION reset
void startSound();      // entering RUNNING
void enterTimeSet();    // entering TIMESET
void lowBatterySound(); // single low-battery beep (used by battery.cpp)

// Confirmation beep played right after sound is turned back on (holding +
// for 3s while PAUSED). Distinct from the other tones above so it's always
// audible at the moment sound is (re-)enabled.
void soundOnBeep();

// Plays the full game-over tune. Blocking — takes a few seconds.
// Set GAME_OVER_SOUND in config.h to swap this for three long beeps instead.
void gameOverTune();
