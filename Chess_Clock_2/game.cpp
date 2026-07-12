#include <Arduino.h>
#include <EEPROM.h>     // EEPROM support to save current preset selection
#include <limits.h>     // just for ULONG_MAX (0xFFFFFFFFUL) definition
#include "game.h"
#include "config.h"
#include "battery.h"    // battery voltage reading and low-battery warnings
#include "sound.h"      // buzzer tones and game-over melody
#include "display.h"    // 7-segment display rendering
#include "rtc.h"        // optional DS3231: corrects millis() drift, no config needed

/* ================= Presets ================= */
// Presets for BULLET, BLITZ, RAPID and CLASSICAL (to 90 minutes)
struct TimePreset {
    uint16_t minutes;
    uint8_t bonus;
};

#define PRESET_COUNT 20
#define EEPROM_PRESET_ADDR 0

static TimePreset presets[PRESET_COUNT] = {
    {1, 0}, {1, 1}, {2, 0}, {2, 1},
    {3, 0}, {3, 2}, {5, 0}, {5, 3},
    {10, 0}, {10, 5}, {15, 0}, {15, 10},
    {30, 0}, {30, 15}, {45, 0}, {45, 30},
    {60, 0}, {60, 30}, {90, 0}, {90, 30}
};

static uint8_t preset_index = 9;
static uint8_t timesetStage = 0;

/* ================= Game state ================= */

static unsigned long base_time;
static unsigned long bonus_time;
static unsigned long player1_time;
static unsigned long player2_time;
static unsigned long turn_start_time;

static bool player1_turn = false;
static bool gameOverPlayed = false;

// To avoid excessive display updates
static unsigned long lastDrawnSeconds1 = ULONG_MAX;
static unsigned long lastDrawnSeconds2 = ULONG_MAX;

// Hysteresis state for the MM:SS/H:MM format switch on the running
// countdown (see updateRunningDisplay() in display.h/.cpp).
static bool hourFormat1 = false;
static bool hourFormat2 = false;

// Include bonus in first turn (hardwired)
static bool includeBonusInFirstTurn = true; // if false, 10:05 will start at 10:00 and increment bonus after

// Used by handlePaused/handleTimeset to swallow the PAUSE release that
// follows leaving TIMESET, so it isn't also treated as "resume game".
static bool ignorePauseRelease = false;

/* ================= Finite-state machine ================= */
// Clock states
enum ClockState {
    SELECT_DURATION,
    PAUSED,
    RUNNING,
    TIMESET,
    GAME_OVER
};

static ClockState state;

/* ================= Game helper functions ================= */
static bool isPlayer1Turn() {
    return digitalRead(PLY2) == LOW;
}

// Converts a preset's stored minutes/bonus into total ms, for showPresetSelect().
static unsigned long presetTimeMs(uint8_t idx) {
    return (presets[idx].minutes * 60UL + presets[idx].bonus) * 1000UL;
}

// Apply preset before starting game
static void applyPreset() {
    base_time = (presets[preset_index].minutes * 60UL) * 1000UL;
    bonus_time = presets[preset_index].bonus * 1000UL;

    player1_time = base_time + (includeBonusInFirstTurn ? bonus_time : 0);
    player2_time = base_time + (includeBonusInFirstTurn ? bonus_time : 0);

    hourFormat1 = false;
    hourFormat2 = false;
}

/* ================= State handlers - called from switch ================= */
// SELECT_DURATION
static void handleSelectDuration(bool plusNow, bool minusNow, bool prevPlus, bool prevMinus, bool pauseReleased,
                                 Adafruit_7segment &d1, Adafruit_7segment &d2) {
    if (PRESSED(plusNow, prevPlus)) {
        preset_index = (preset_index + 1) % PRESET_COUNT;
        EEPROM.update(EEPROM_PRESET_ADDR, preset_index);
        clickSound();
        showPresetSelect(preset_index, presetTimeMs(preset_index), d1, d2);
    }
    if (PRESSED(minusNow, prevMinus)) {
        preset_index = (preset_index + PRESET_COUNT - 1) % PRESET_COUNT;
        EEPROM.update(EEPROM_PRESET_ADDR, preset_index);
        clickSound();
        showPresetSelect(preset_index, presetTimeMs(preset_index), d1, d2);
    }
    if (pauseReleased) {
        applyPreset();
        showPausedDisplays(d1, player1_time, d2, player2_time);
        state = PAUSED;
    }
}

// PAUSED
static void handlePaused(bool pauseReleased, bool pauseLong, Adafruit_7segment &d1, Adafruit_7segment &d2) {
    checkBattery();
    if (ignorePauseRelease) {
        if (pauseReleased) {
            ignorePauseRelease = false;
        }
        return;
    }
    if (pauseLong) {
        enterTimeSet();
        timesetStage = 0;
        showTimeSetDisplay(timesetStage, d1, player1_time, d2, player2_time);
        state = TIMESET;
    } else if (pauseReleased) {
        player1_turn = isPlayer1Turn();
        turn_start_time = correctedMillis();
        startSound();
        state = RUNNING;
    }
}

// RUNNING
static void handleRunning(bool pauseReleased, bool pauseLongFired, bool p1Now, bool p2Now, bool prevP1, bool prevP2,
                   Adafruit_7segment &d1, Adafruit_7segment &d2) {
    checkBattery();
    unsigned long elapsed = correctedMillis() - turn_start_time;

    unsigned long *activeTime = player1_turn ? &player1_time : &player2_time;
    Adafruit_7segment *activeDisplay = player1_turn ? &d1 : &d2;
    unsigned long *tracker = player1_turn ? &lastDrawnSeconds1 : &lastDrawnSeconds2;
    bool *activeHourFormat = player1_turn ? &hourFormat1 : &hourFormat2;

    if (*activeTime > elapsed) {
        unsigned long remaining = *activeTime - elapsed;
        unsigned long secs = remaining / 1000;
        if (secs != *tracker) {
            *activeHourFormat = updateRunningDisplay(*activeDisplay, remaining, *activeHourFormat);
            *tracker = secs;
        }
    } else {
        updateDisplay(*activeDisplay, 0);
        gameOverPlayed = false;
        state = GAME_OVER;
        return;
    }

    // Turn ends when the active player's own switch fully latches (normal case)
    // OR when the switch currently holding this turn releases, even if the
    // opposite switch never fully latches (seesaw stuck in the mechanical gap).
    // Scoping by player1_turn prevents a stray press on the "wrong" switch
    // from re-triggering a flip after the turn has already changed.
    bool turnEnded = player1_turn
                     ? (PRESSED(p1Now, prevP1) || RELEASED(p2Now, prevP2))
                     : (PRESSED(p2Now, prevP2) || RELEASED(p1Now, prevP1));

    if (turnEnded && elapsed > 60) {
        *activeTime = (*activeTime > elapsed) ? *activeTime - elapsed : 0;
        *activeTime += bonus_time;
        lastDrawnSeconds1 = ULONG_MAX;
        lastDrawnSeconds2 = ULONG_MAX;
        *activeHourFormat = updateRunningDisplay(*activeDisplay, *activeTime, *activeHourFormat);

        player1_turn = !player1_turn;
        turn_start_time = correctedMillis();

        unsigned long *otherTime = player1_turn ? &player1_time : &player2_time;
        Adafruit_7segment *otherDisplay = player1_turn ? &d1 : &d2;
        bool *otherHourFormat = player1_turn ? &hourFormat1 : &hourFormat2;
        *otherHourFormat = updateRunningDisplay(*otherDisplay, *otherTime, *otherHourFormat);
    }

    if (pauseReleased && !pauseLongFired) {
        *activeTime = (*activeTime > elapsed) ? *activeTime - elapsed : 0;
        lastDrawnSeconds1 = ULONG_MAX;
        lastDrawnSeconds2 = ULONG_MAX;
        *activeHourFormat = updateRunningDisplay(*activeDisplay, *activeTime, *activeHourFormat);
        modeSound();
        state = PAUSED;
    }
}

// TIMESET
static void handleTimeset(bool pausePressed, bool plusNow, bool prevPlus, bool minusNow, bool prevMinus,
                   Adafruit_7segment &d1, Adafruit_7segment &d2) {
    if (pausePressed) {
        timesetStage++;
        if (timesetStage > 1) {
            showPausedDisplays(d1, player1_time, d2, player2_time);
            ignorePauseRelease = true;
            state = PAUSED;
        } else {
            showTimeSetDisplay(timesetStage, d1, player1_time, d2, player2_time);
        }
    }
    if (PRESSED(plusNow, prevPlus)) {
        clickSound();
        if (timesetStage == 0) {
            player1_time += 60000UL;
        } else {
            player2_time += 60000UL;
        }
        showTimeSetDisplay(timesetStage, d1, player1_time, d2, player2_time);
    }
    if (PRESSED(minusNow, prevMinus)) {
        clickSound();
        unsigned long &t = (timesetStage == 0) ? player1_time : player2_time;
        if (t >= 60000UL) t -= 60000UL;
            showTimeSetDisplay(timesetStage, d1, player1_time, d2, player2_time);
        }
    }

// GAME_OVER
static void handleGameOver(bool pauseReleased, Adafruit_7segment &d1, Adafruit_7segment &d2) {
    if (!gameOverPlayed) {
        gameOverTune();
        gameOverPlayed = true;
    }
    if (pauseReleased) {
        applyPreset();
        showPausedDisplays(d1, player1_time, d2, player2_time);
        state = PAUSED;
    }
}

/* ================= Public API ================= */

// Called from setup() to initialize the game state machine and display the preset selection screen.
void gameInit(Adafruit_7segment &d1, Adafruit_7segment &d2) {
    uint8_t stored = EEPROM.read(EEPROM_PRESET_ADDR); // Get preset selection if it exists
    if (stored < PRESET_COUNT)
        preset_index = stored;

    applyPreset();
    showPresetSelect(preset_index, presetTimeMs(preset_index), d1, d2);
    state = SELECT_DURATION;
}

void gameResetToSelectDuration(Adafruit_7segment &d1, Adafruit_7segment &d2) {
    applyPreset();
    showPresetSelect(preset_index, presetTimeMs(preset_index), d1, d2);
    modeSound();
    state = SELECT_DURATION;
}

void gameUpdate(bool plusNow, bool prevPlus, bool minusNow, bool prevMinus,
                bool pausePressed, bool pauseReleased,
                bool pauseLong, bool pauseLongFired,
                bool p1Now, bool prevP1, bool p2Now, bool prevP2,
                Adafruit_7segment &d1, Adafruit_7segment &d2) {

    switch (state) {
      case SELECT_DURATION:
        handleSelectDuration(plusNow, minusNow, prevPlus, prevMinus, pauseReleased, d1, d2);
        break;
      case PAUSED:
        handlePaused(pauseReleased, pauseLong, d1, d2);
        break;
      case RUNNING:
        handleRunning(pauseReleased, pauseLongFired, p1Now, p2Now, prevP1, prevP2, d1, d2);
        break;
      case TIMESET:
        handleTimeset(pausePressed, plusNow, prevPlus, minusNow, prevMinus, d1, d2);
        break;
      case GAME_OVER:
        handleGameOver(pauseReleased, d1, d2);
        break;
    }
}

// Getters
bool gameIsRunning()           { return state == RUNNING; }
bool gameIsSelectingDuration() { return state == SELECT_DURATION; }
uint8_t gameGetPresetIndex()   { return preset_index; }
unsigned long gameGetPresetTimeMs() { return presetTimeMs(preset_index); }
unsigned long gameGetPlayer1Time()  { return player1_time; }
unsigned long gameGetPlayer2Time()  { return player2_time; }


