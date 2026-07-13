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

// 20 duration presets cover a range of common chess time controls, from bullet to classical.
#define PRESET_COUNT 20

// Presets are stored in EEPROM at address 0, so they can be remembered across power cycles.
#define EEPROM_PRESET_ADDR 0

// The presets are in order of increasing total time (minutes + bonus), so the user can scroll through them with the + and - buttons.
// The presets are: 1:00, 1:01, 2:00, 2:01, 3:00, 3:02, 5:00, 5:03, 10:00, 10:05, 15:00, 15:10, 30:00, 30:15, 45:00, 45:30, 60:00, 60:30, 90:00, 90:30
// The bonus is in seconds, so 1:01 is 1 minute and 1 second, 2:01 is 2 minutes and 1 second, etc.
static TimePreset presets[PRESET_COUNT] = {
    {1, 0}, {1, 1}, {2, 0}, {2, 1},
    {3, 0}, {3, 2}, {5, 0}, {5, 3},
    {10, 0}, {10, 5}, {15, 0}, {15, 10},
    {30, 0}, {30, 15}, {45, 0}, {45, 30},
    {60, 0}, {60, 30}, {90, 0}, {90, 30}
};

// The factory default is the 10:05 preset (index 9)
static uint8_t preset_index = 9;

// The TIMESET state has two stages: 0 for player 1, 1 for player 2. This variable tracks which stage we're in.
// It is reset to 0 when entering TIMESET, and incremented when the user presses PAUSE to move to the next stage.
// When the user presses PAUSE again after stage 1, we exit TIMESET and return to PAUSED.
// Pressing + or - during TIMESET adjusts the time for the current stage (player 1 or player 2).
static uint8_t timesetStage = 0;

/* ================= Game state ================= */
static unsigned long base_time;         // The base time for each player, in milliseconds, derived from the selected preset's minutes.
static unsigned long bonus_time;        // The bonus time for each player, in milliseconds, derived from the selected preset's bonus seconds.
static unsigned long player1_time;      // The remaining time for player 1, in milliseconds.
static unsigned long player2_time;      // The remaining time for player 2, in milliseconds.
static unsigned long turn_start_time;   // The time when the current turn started, in milliseconds, used to calculate elapsed time for the active player.

static bool player1_turn = false;       // True if it's player 1's turn, false if it's player 2's turn. Determined by the seesaw switch state when the game starts or resumes.
static bool gameOverPlayed = false;     // True if the game-over melody has been played, false otherwise. Prevents the melody from playing repeatedly when the game is over.

// To avoid excessive display updates
static unsigned long lastDrawnSeconds1 = ULONG_MAX;     // The last drawn seconds for player 1's display, used to determine if the display needs to be updated. Initialized to ULONG_MAX to force an initial update.
static unsigned long lastDrawnSeconds2 = ULONG_MAX;     // Same data for player 2

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
// The game has five states: SELECT_DURATION, PAUSED, RUNNING, TIMESET, and GAME_OVER.
// The state machine transitions between these states based on user input (button presses)
// and game events (time running out).

// Clock states
enum ClockState {
    SELECT_DURATION,    // The user can select a preset duration with the + and - buttons, then press PAUSE to start the game.
    PAUSED,             // The game is paused, and the user can press PAUSE to resume, or long-press PAUSE to enter TIMESET.
    RUNNING,            // The game is running, and the user can press PAUSE to pause, or tap the seesaw switch to end their turn.
    TIMESET,            // The user can set the time for each player with the + and - buttons, then press PAUSE to return to PAUSED.
    GAME_OVER           // One player's time has run out, and the user can press PAUSE to reset to PAUSED.
};

static ClockState state;

// Returns true if it's player 1's turn, false if it's player 2's turn.
// Determined by the seesaw switch state when the game starts or resumes.
static bool isPlayer1Turn() {
    return digitalRead(PLY2) == LOW;
}

/* ================= Preset controls ================= */
// Converts a preset's stored minutes/bonus into total ms, for showPresetSelect() (see display.h / display.cpp)
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

/* ================= State handlers - called from clockUpdate() ================= */

// These functions receive button states and display objects, and handle the logic for each state.
// They are called from clockUpdate() based on the current state. The PRESSED and RELEASED macros are used to
// detect button presses and releases based on the current and previous states of the buttons.
// See ClockState enum and clockUpdate() for more details.

// SELECT_DURATION
static void handleSelectDuration(bool plusNow, bool minusNow, bool prevPlus, bool prevMinus, bool pauseReleased,
                                 Adafruit_7segment &d1, Adafruit_7segment &d2) {
    if (PRESSED(plusNow, prevPlus)) {
        preset_index = (preset_index + 1) % PRESET_COUNT;   //
        EEPROM.update(EEPROM_PRESET_ADDR, preset_index);    // Save preset selection to EEPROM
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
    checkBattery();                         // Check battery voltage and play low-battery warning if needed
    if (ignorePauseRelease) {               // Ignore the PAUSE release that follows leaving TIMESET, so it isn't also treated as "resume game".
        if (pauseReleased) {
            ignorePauseRelease = false;
        }
        return;
    }
    if (pauseLong) {
        enterTimeSet();
        timesetStage = 0;   // Start with player 1's time
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
// This function handles the main game logic for the chess clock.
static void handleRunning(bool pauseReleased, bool pauseLongFired, bool p1Now, bool p2Now, bool prevP1, bool prevP2,
                          Adafruit_7segment &d1, Adafruit_7segment &d2) {
    checkBattery();
    unsigned long elapsed = correctedMillis() - turn_start_time;

    // Update ACTIVE player pointers based on whose turn it is.
    unsigned long *activeTime = player1_turn ? &player1_time : &player2_time;   // Pointer to the active player's remaining time
    Adafruit_7segment *activeDisplay = player1_turn ? &d1 : &d2;    // Pointer to the active player's display
    unsigned long *tracker = player1_turn ? &lastDrawnSeconds1 : &lastDrawnSeconds2;  // Pointer to the active player's last drawn seconds
    bool *activeHourFormat = player1_turn ? &hourFormat1 : &hourFormat2;    // Pointer to the active player's hour format state

    // Update the active player's display if the remaining time has changed since the last update.
    // The display is updated every second in MM:SS format, and every half second in H:MM format (to blink the colon).
    // The *tracker variable is used to track last seconds rendered, so we only update the display when seconds change.
    if (*activeTime > elapsed) {
        unsigned long remaining = *activeTime - elapsed;
        unsigned long tickMs = *activeHourFormat ? 500UL : 1000UL;
        unsigned long tick = remaining / tickMs;
        if (tick != *tracker) { // Only update the display if the seconds have changed since the last update
            *activeHourFormat = updateRunningDisplay(*activeDisplay, remaining, *activeHourFormat);
            *tracker = tick;
        }
    } else {    // Time has run out for the active player
        updateDisplay(*activeDisplay, 0);
        gameOverPlayed = false;
        state = GAME_OVER;
        return;
    }

    // This mechanical clock consists of a seesaw with two micro switches, one for each player.
    // When a player presses their switch, it latches and the other player's switch is released.
    // The turn ends when the active player's own switch fully latches (normal case) or when the
    // switch currently holding this turn releases, even if the opposite switch never fully latches
    // (seesaw stuck in the mechanical gap). The turnEnded variable is used to detect when the turn
    // has ended based on the current and previous states of the switches.
    bool turnEnded = player1_turn
                     ? (PRESSED(p1Now, prevP1) || RELEASED(p2Now, prevP2))  // Player 1's turn ends if player 1's switch is pressed or player 2's switch is released
                     : (PRESSED(p2Now, prevP2) || RELEASED(p1Now, prevP1)); // Player 2's turn ends if player 2's switch is pressed or player 1's switch is released

    // This is the main logic for ending a turn.
    if (turnEnded && elapsed > 60) {    // Only end turn if >60ms have elapsed (protect against switch bounce)
        *activeTime = (*activeTime > elapsed) ? *activeTime - elapsed : 0;  // Deduct elapsed from active player's remaining time, but don't go below 0
        *activeTime += bonus_time;  // Add the bonus time to active player's remaining time
        lastDrawnSeconds1 = ULONG_MAX;
        lastDrawnSeconds2 = ULONG_MAX;
        *activeHourFormat = updateRunningDisplay(*activeDisplay, *activeTime, *activeHourFormat);  // Update the active player's display with the new remaining time and format
        updateDisplay(*activeDisplay, *activeTime); // Update the active player's display with the new remaining time

        player1_turn = !player1_turn;           // Switch to the other player's turn
        turn_start_time = correctedMillis();    // Reset the turn start time for the new active player

        // Update OTHER player pointers based on whose turn it is.
        unsigned long *otherTime = player1_turn ? &player1_time : &player2_time;  // Pointer to the other player's remaining time
        Adafruit_7segment *otherDisplay = player1_turn ? &d1 : &d2;
        bool *otherHourFormat = player1_turn ? &hourFormat1 : &hourFormat2;
        *otherHourFormat = updateRunningDisplay(*otherDisplay, *otherTime, *otherHourFormat);
    }

    // If the user presses PAUSE while the game is running, we pause the game and return to the PAUSED state.
    if (pauseReleased && !pauseLongFired) {
        *activeTime = (*activeTime > elapsed) ? *activeTime - elapsed : 0;
        lastDrawnSeconds1 = ULONG_MAX;
        lastDrawnSeconds2 = ULONG_MAX;
        *activeHourFormat = updateRunningDisplay(*activeDisplay, *activeTime, *activeHourFormat);
        updateDisplay(*activeDisplay, *activeTime);
        modeSound();
        state = PAUSED;
    }
}

// TIMESET
// Used to add or remove minutes at the start or during a game (in PAUSED state).
static void handleTimeset(bool pausePressed, bool plusNow, bool prevPlus, bool minusNow, bool prevMinus,
                          Adafruit_7segment &d1, Adafruit_7segment &d2) {
    if (pausePressed) {
        timesetStage++;
        if (timesetStage > 1) { // Finished setting both players' times, return to PAUSED state
            showPausedDisplays(d1, player1_time, d2, player2_time);
            ignorePauseRelease = true;
            state = PAUSED;
        } else { // Move to the next stage (player 2)
            showTimeSetDisplay(timesetStage, d1, player1_time, d2, player2_time);
        }
    }
    if (PRESSED(plusNow, prevPlus)) {   // Add 1 minute to a player's time (60000 ms)
        clickSound();
        if (timesetStage == 0) {        // Add to player 1's time
            player1_time += 60000UL;
        } else {                        // Add to player 2's time
            player2_time += 60000UL;
        }
        showTimeSetDisplay(timesetStage, d1, player1_time, d2, player2_time);
    }
    if (PRESSED(minusNow, prevMinus)) {
        clickSound();
        unsigned long &t = (timesetStage == 0) ? player1_time : player2_time;
        if (t >= 60000UL) t -= 60000UL; // Subtract 1 minute from a player's time, but don't go below 0
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
// These are the only functions that should be called from the main sketch (Chess_Clock_2.ino).
// They handle the game state machine and display updates based on user input and game events.

// Getters
bool isRunning()           { return state == RUNNING; }
bool isSelectingDuration() { return state == SELECT_DURATION; }

uint8_t getPresetIndex()   { return preset_index; }
unsigned long getPresetTimeMs() { return presetTimeMs(preset_index); }
unsigned long getPlayer1Time()  { return player1_time; }
unsigned long getPlayer2Time()  { return player2_time; }

// Called from setup() to initialize the game state machine and display the preset selection screen.
void clockInit(Adafruit_7segment &d1, Adafruit_7segment &d2) {
    uint8_t stored = EEPROM.read(EEPROM_PRESET_ADDR); // Get preset selection if it exists
    if (stored < PRESET_COUNT) {
        preset_index = stored;
    }

    applyPreset();
    showPresetSelect(preset_index, presetTimeMs(preset_index), d1, d2);
    state = SELECT_DURATION;
}

// User can reset by pressing + and - together during any state except RUNNING.
void resetToSelectDuration(Adafruit_7segment &d1, Adafruit_7segment &d2) {
    applyPreset();
    showPresetSelect(preset_index, presetTimeMs(preset_index), d1, d2);
    modeSound();
    state = SELECT_DURATION;
}

// This is the main game loop, called from loop() in the main sketch.
// It handles the state machine and display updates based on user input and game events.
void clockUpdate(bool plusNow, bool prevPlus, bool minusNow, bool prevMinus, // state of + and - buttons
                 bool pausePressed, bool pauseReleased,                      // state of PAUSE button
                 bool pauseLong, bool pauseLongFired,
                 bool p1Now, bool prevP1, bool p2Now, bool prevP2,           // state of seesaw switches
                 Adafruit_7segment &d1, Adafruit_7segment &d2) {             // displays for player 1 and player 2

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


