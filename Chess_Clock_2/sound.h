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

// Short feedback tones
void clickSound();      // preset/time-set +/- press
void modeSound();       // entering PAUSED from RUNNING, or SELECT_DURATION reset
void startSound();      // entering RUNNING
void enterTimeSet();    // entering TIMESET
void lowBatterySound(); // single low-battery beep (used by battery.cpp)

// Plays the full game-over tune. Blocking — takes a few seconds.
void gameOverTune();
