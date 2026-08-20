/* ================================================================
 * game.h — Game state machine (presets, turn timing, TIMESET, etc.)
 *
 * Everything about presets and the clock's internal state is kept
 * private to game.cpp. The .ino only drives this through the small
 * API below and never needs to know the state machine's shape.
 *
 * Created by Helder da Rocha on 12/07/26.
================================================================ */

//#ifndef CHESS_CLOCKS_ARDUINO_GAME_H
//#define CHESS_CLOCKS_ARDUINO_GAME_H
//#endif //CHESS_CLOCKS_ARDUINO_GAME_H
#pragma once

#include <Adafruit_LEDBackpack.h>

// Macro to detect a button press edge (HIGH -> LOW) with INPUT_PULLUP.
// Necessary to avoid triggering multiple times while a button is held down.
#define PRESSED(n,p) ((n)==LOW && (p)==HIGH)

// Macro to detect a released edge - needed to handle inconsistent states
// (e.g. two switches briefly both open during a seesaw's mechanical gap).
#define RELEASED(n,p) ((n)==HIGH && (p)==LOW)

// Getters used by loop() for idle/sleep timing and to redraw after waking
bool isRunning();
bool isSelectingDuration();
bool isPaused();
uint8_t getPresetIndex();
unsigned long getPresetTimeMs();
unsigned long getPlayer1Time();
unsigned long getPlayer2Time();

// True while the clock is showing the move-count/total-time screen (see
// showGameStats() in display.h), i.e. the brief MINUS_BTN-triggered screen
// reachable from PAUSED. loop() also treats this like PAUSED for the
// PAUSE LED, since the game is still effectively paused, not running.
bool isShowingMoveInfo();

// Number of moves completed so far this game, and total time the clock
// has actually been running (RUNNING state only — paused time is not
// counted). Both reset to 0 whenever a new game starts (preset applied
// via SELECT_DURATION, the +/- reset combo, or after GAME_OVER). A "move"
// is counted each time the starting ("white") player ends their own turn;
// see game.cpp for details.
unsigned long getMoveCount();
unsigned long getTotalPlayTimeMs();

// Loads the persisted preset from EEPROM (if any), applies it, draws the
// preset-selection screen, and enters the initial state. Call once from
// setup(), after the displays are initialized.
void clockInit(Adafruit_7segment &d1, Adafruit_7segment &d2);

// Re-applies the current preset, redraws the preset-selection screen, plays
// the mode-change sound, and returns to preset-selection. Used by the +/-
// long-press reset combo.
void resetToSelectDuration(Adafruit_7segment &d1, Adafruit_7segment &d2);

/**
 * Advances the game state machine by one step. Call once per loop() iteration (skip right after waking from sleep).
 * @plusNow/@minusNow/@p1Now/@p2Now and their @prev counterparts - the raw button reads;
 * @pausePressed/@pauseReleased/@pauseLong/@pauseLongFired - the edge/hold signals computed by loop()'s own
 *     PAUSE-button timing logic.
 * @minusReleased/@minusLongFired - the release edge and persistent hold-fired flag for the MINUS button,
 *     analogous to pauseReleased/pauseLongFired. They're used while PAUSED: a short tap on the MINUS
 *     button (released before the 3s sound-toggle hold fires) opens the move-info screen; the existing
 *     minusLong hold-to-mute behavior is unaffected.
*/
void clockUpdate(bool plusNow, bool prevPlus, bool minusNow, bool prevMinus,
                 bool pausePressed, bool pauseReleased,
                 bool pauseLong, bool pauseLongFired,
                 bool plusLong, bool minusLong,
                 bool minusReleased, bool minusLongFired,
                 bool p1Now, bool prevP1, bool p2Now, bool prevP2,
                 Adafruit_7segment &d1, Adafruit_7segment &d2);
