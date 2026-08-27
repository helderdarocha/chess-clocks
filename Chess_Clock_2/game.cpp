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
#define PRESET_COUNT 21

// Presets are stored in EEPROM at address 0, so they can be remembered across power cycles.
#define EEPROM_PRESET_ADDR 0

// The user-editable custom preset (index PRESET_COUNT-1) is persisted
// separately: its minutes (0-599, so it needs two bytes) at addresses 2-3,
// and its bonus seconds (0-59, one byte) at address 4. Address 1 is already
// used by sound.cpp for the sound on/off preference, so these don't collide
// with it or with EEPROM_PRESET_ADDR.
#define EEPROM_CUSTOM_MINUTES_ADDR 2
#define EEPROM_CUSTOM_BONUS_ADDR   4

// The first 20 presets are in order of increasing total time (minutes + bonus), so the user can
// scroll through them with the + and - buttons.
// The presets are: 1:00, 1:01, 2:00, 2:01, 3:00, 3:02, 5:00, 5:03, 10:00, 10:05, 15:00, 15:10, 30:00, 30:15, 45:00, 45:30, 60:00, 60:30, 90:00, 90:30
// The bonus is in seconds, so 1:01 is 1 minute and 1 second, 2:01 is 2 minutes and 1 second, etc.
// The 21st entry ({0,0}) is a placeholder for the custom preset; clockInit()
// overwrites it with whatever was last saved in EEPROM, if anything.
static TimePreset presets[PRESET_COUNT] = {
    {1, 0}, {1, 1}, {2, 0}, {2, 1},
    {3, 0}, {3, 2}, {5, 0}, {5, 3},
    {10, 0}, {10, 5}, {15, 0}, {15, 10},
    {30, 0}, {30, 15}, {45, 0}, {45, 30},
    {60, 0}, {60, 30}, {90, 0}, {90, 30},
    {0, 0}
};

// The factory default is the 10:05 preset (index 9)
static uint8_t preset_index = 9;

// The TIMESET state has two stages: 0 for player 1, 1 for player 2. This variable tracks which stage we're in.
// It is reset to 0 when entering TIMESET, and incremented when the user presses PAUSE to move to the next stage.
// When the user presses PAUSE again after stage 1, we exit TIMESET and return to PAUSED.
// Pressing + or - during TIMESET adjusts the time for the current stage (player 1 or player 2).
static uint8_t timesetStage = 0;

// PRESET_EDIT state: lets the user build a custom preset (saved as preset
// PRESET_COUNT-1, "preset 20") one digit at a time. Entered by long-pressing
// PAUSE while in SELECT_DURATION. editField walks through the five digits in
// order (see the PRESET_EDIT_* constants in display.h) via PAUSE presses;
// + and - change the current digit; editBlinkOn/editBlinkMs drive the
// flash on whichever digit is currently being edited.
static uint8_t editField = PRESET_EDIT_HOURS;
static uint8_t editHours = 0, editMinTens = 0, editMinOnes = 0;
static uint8_t editBonusTens = 0, editBonusOnes = 0;
static unsigned long editBlinkMs = 0;
static bool editBlinkOn = true;

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

/* ================= Move counting / total playing time ================= */
// "White" is whichever player is active the first time this game enters
// RUNNING (not re-determined on every pause/resume, so it stays fixed even
// if the seesaw position is nudged while paused). A "move" is counted each
// time White's own turn ends — i.e. when White presses their lever, mirroring
// standard chess notation where a move number appears once White has played
// (Black's reply is still part of that same numbered move).
static bool startingPlayerIsPlayer1 = true;
static bool gameStarted = false;            // true once this game has entered RUNNING at least once
static unsigned long moveCount = 0;
static unsigned long totalPlayTimeMs = 0;   // sum of RUNNING time only; paused time is never added

// millis() timestamp when SHOW_MOVE_INFO was entered, used for its 5s auto-timeout.
static unsigned long moveInfoEnteredMs = 0;

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
    GAME_OVER,          // One player's time has run out, and the user can press PAUSE to reset to PAUSED.
    SHOW_MOVE_INFO,     // Briefly shows total game time + move count; reached from PAUSED via "-", returns to PAUSED after 5s or another "-" tap.
    PRESET_EDIT         // Digit-by-digit editor for the custom preset (index PRESET_COUNT-1); reached from SELECT_DURATION via a long PAUSE press.
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

    // A fresh preset means a fresh game: reset move/time tracking. Who
    // "White" is gets (re-)determined the next time RUNNING is entered.
    gameStarted = false;
    moveCount = 0;
    totalPlayTimeMs = 0;
}

/* ================= State handlers - called from clockUpdate() ================= */

// These functions receive button states and display objects, and handle the logic for each state.
// They are called from clockUpdate() based on the current state. The PRESSED and RELEASED macros are used to
// detect button presses and releases based on the current and previous states of the buttons.
// See ClockState enum and clockUpdate() for more details.

// Loads the five edit digits from a preset's stored minutes/bonus and
// (re)draws the editor at the first field. Shared by the entry path below
// and could be reused if a "restart edit" shortcut is ever added.
static void enterPresetEdit(uint8_t sourcePresetIdx, Adafruit_7segment &d1, Adafruit_7segment &d2) {
    unsigned long totalMin = presets[sourcePresetIdx].minutes;  // 0-599, validated at load time
    editHours    = totalMin / 60;
    editMinTens  = (totalMin % 60) / 10;
    editMinOnes  = (totalMin % 60) % 10;
    editBonusTens = presets[sourcePresetIdx].bonus / 10;
    editBonusOnes = presets[sourcePresetIdx].bonus % 10;

    editField = PRESET_EDIT_HOURS;
    editBlinkOn = true;
    editBlinkMs = millis();
    showPresetEditDisplay(editField, editHours, editMinTens, editMinOnes,
                          editBonusTens, editBonusOnes, true, d1, d2);
}

// SELECT_DURATION
static void handleSelectDuration(bool plusNow, bool minusNow, bool prevPlus, bool prevMinus,
                                 bool pauseReleased, bool pauseLong,
                                 Adafruit_7segment &d1, Adafruit_7segment &d2) {
    if (ignorePauseRelease) {
        if (pauseReleased) ignorePauseRelease = false;
        return;
    }
    if (pauseLong) {
        modeSound();
        enterPresetEdit(PRESET_COUNT - 1, d1, d2);
        state = PRESET_EDIT;
        return;
    }
    if (PRESSED(plusNow, prevPlus)) {
        preset_index = (preset_index + 1) % PRESET_COUNT;
        clickSound();
        showPresetSelect(preset_index, presetTimeMs(preset_index), d1, d2);
    }
    if (PRESSED(minusNow, prevMinus)) {
        preset_index = (preset_index + PRESET_COUNT - 1) % PRESET_COUNT;
        clickSound();
        showPresetSelect(preset_index, presetTimeMs(preset_index), d1, d2);
    }
    if (pauseReleased) {
        EEPROM.update(EEPROM_PRESET_ADDR, preset_index);  // only write once confirmed
        applyPreset();
        showPausedDisplays(d1, player1_time, d2, player2_time);
        state = PAUSED;
    }
}

// PAUSED
static void handlePaused(bool pauseReleased, bool pauseLong, bool plusLong, bool minusLong,
                         bool minusReleased, bool minusLongFired,
                         Adafruit_7segment &d1, Adafruit_7segment &d2) {
    checkBattery();                         // Check battery voltage and play low-battery warning if needed
    if (ignorePauseRelease) {               // Ignore the PAUSE release that follows leaving TIMESET, so it isn't also treated as "resume game".
        if (pauseReleased) {
            ignorePauseRelease = false;
        }
        return;
    }
    // A short "-" tap (released before the 3s hold-to-mute threshold fires)
    // opens the move-info screen: total game time + current move number.
    // Checked before the minusLong hold-toggle below so the two can't both
    // act on the same press (minusLongFired stays true through this frame
    // if the hold already fired the mute action).
    if (minusReleased && !minusLongFired) {
        moveInfoEnteredMs = millis();
        showGameStats(totalPlayTimeMs, moveCount, d1, d2);
        state = SHOW_MOVE_INFO;
        return;
    }
    // Holding + or - for 3s toggles sound on/off, showing "Snd 0n"/"Snd 0ff"
    // for 2 seconds before returning to the normal paused display.
    if (plusLong) {
        setSoundEnabled(true);
        showSoundStatus(true, d1, d2);
        soundOnBeep();
        delay(2000);
        showPausedDisplays(d1, player1_time, d2, player2_time);
        return;
    }
    if (minusLong) {
        setSoundEnabled(false);
        showSoundStatus(false, d1, d2);
        delay(2000);
        showPausedDisplays(d1, player1_time, d2, player2_time);
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
        // The first time this game ever enters RUNNING, lock in whichever
        // player is active as "White" for move-counting purposes. Later
        // pause/resume cycles within the same game leave this alone.
        if (!gameStarted) {
            startingPlayerIsPlayer1 = player1_turn;
            gameStarted = true;
        }
        startSound();
        state = RUNNING;
    }
}

// SHOW_MOVE_INFO
// Briefly shows total game time + move count (see handlePaused()). Exits
// back to PAUSED either on another "-" tap or automatically after 5s.
static void handleShowMoveInfo(bool minusReleased, Adafruit_7segment &d1, Adafruit_7segment &d2) {
    if (minusReleased || millis() - moveInfoEnteredMs >= 5000UL) {
        showPausedDisplays(d1, player1_time, d2, player2_time);
        state = PAUSED;
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
        // Credit whatever time the active player had left: all of it was
        // spent (ticked down to zero) before the game ended, even though
        // they never pressed their lever to complete this final turn — so
        // it counts toward total playing time but not toward moveCount.
        totalPlayTimeMs += *activeTime;
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
        totalPlayTimeMs += elapsed;   // this turn's active time counts toward total playing time
        if (player1_turn == startingPlayerIsPlayer1) {
            moveCount++;   // White's turn just ended: that's one more completed move
        }
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
        totalPlayTimeMs += elapsed;   // time spent on this (incomplete) turn still counts; pausing itself doesn't
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

// PRESET_EDIT
// Lets the user build a custom time control one digit at a time: hours (0-9),
// minutes tens (0-5), minutes ones (0-9), then bonus tens (0-5) and bonus
// ones (0-9). + and - change the current digit (wrapping); a PAUSE press
// advances to the next digit; after the last one, PAUSE saves the result as
// preset PRESET_COUNT-1 ("preset 20") to EEPROM, selects it, and returns to
// SELECT_DURATION showing it like any other preset.
static void handlePresetEdit(bool pausePressed, bool plusNow, bool prevPlus, bool minusNow, bool prevMinus,
                             Adafruit_7segment &d1, Adafruit_7segment &d2) {
    // Flash the digit currently being edited at a steady rate, independent
    // of button presses (mirrors the running-clock colon blink in spirit).
    unsigned long now = millis();
    if (now - editBlinkMs >= 300UL) {
        editBlinkMs = now;
        editBlinkOn = !editBlinkOn;
        showPresetEditDisplay(editField, editHours, editMinTens, editMinOnes,
                              editBonusTens, editBonusOnes, editBlinkOn, d1, d2);
    }

    // Which variable the current field edits, and its wraparound ceiling
    // (5 for the two "tens" digits of a sexagesimal field, 9 otherwise).
    uint8_t *digit;
    uint8_t maxVal;
    switch (editField) {
        case PRESET_EDIT_MIN_TENS:   digit = &editMinTens;   maxVal = 5; break;
        case PRESET_EDIT_MIN_ONES:   digit = &editMinOnes;   maxVal = 9; break;
        case PRESET_EDIT_BONUS_TENS: digit = &editBonusTens; maxVal = 5; break;
        case PRESET_EDIT_BONUS_ONES: digit = &editBonusOnes; maxVal = 9; break;
        default:                     digit = &editHours;     maxVal = 9; break;  // PRESET_EDIT_HOURS
    }

    if (PRESSED(plusNow, prevPlus) || PRESSED(minusNow, prevMinus)) {
        clickSound();
        if (PRESSED(plusNow, prevPlus)) {
            *digit = (*digit >= maxVal) ? 0 : *digit + 1;
        } else {
            *digit = (*digit == 0) ? maxVal : *digit - 1;
        }
        // Force the just-changed digit fully visible right away, rather
        // than leaving it mid-blink until the next 300ms tick.
        editBlinkOn = true;
        editBlinkMs = millis();
        showPresetEditDisplay(editField, editHours, editMinTens, editMinOnes,
                              editBonusTens, editBonusOnes, true, d1, d2);
    }

    if (pausePressed) {
        clickSound();
        if (editField < PRESET_EDIT_BONUS_ONES) {
            editField++;
            editBlinkOn = true;
            editBlinkMs = millis();
            showPresetEditDisplay(editField, editHours, editMinTens, editMinOnes,
                                  editBonusTens, editBonusOnes, true, d1, d2);
        } else {
            // Last digit confirmed: assemble and save the new preset.
            uint8_t custom = PRESET_COUNT - 1;
            uint16_t totalMinutes = (uint16_t)editHours * 60 + editMinTens * 10 + editMinOnes;
            uint8_t bonusSec = editBonusTens * 10 + editBonusOnes;

            presets[custom].minutes = totalMinutes;
            presets[custom].bonus = bonusSec;
            EEPROM.put(EEPROM_CUSTOM_MINUTES_ADDR, totalMinutes);
            EEPROM.update(EEPROM_CUSTOM_BONUS_ADDR, bonusSec);

            preset_index = custom;
            EEPROM.update(EEPROM_PRESET_ADDR, preset_index);  // remember it's selected, like picking any other preset

            modeSound();
            showPresetSelect(preset_index, presetTimeMs(preset_index), d1, d2);
            ignorePauseRelease = true;  // swallow this same press's release once we're back in SELECT_DURATION
            state = SELECT_DURATION;
        }
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
bool isPaused()            { return state == PAUSED; }
bool isShowingMoveInfo()   { return state == SHOW_MOVE_INFO; }

uint8_t getPresetIndex()   { return preset_index; }
unsigned long getPresetTimeMs() { return presetTimeMs(preset_index); }
unsigned long getPlayer1Time()  { return player1_time; }
unsigned long getPlayer2Time()  { return player2_time; }
unsigned long getMoveCount()       { return moveCount; }
unsigned long getTotalPlayTimeMs() { return totalPlayTimeMs; }

// Called from setup() to initialize the game state machine and display the preset selection screen.
void clockInit(Adafruit_7segment &d1, Adafruit_7segment &d2) {
    uint8_t stored = EEPROM.read(EEPROM_PRESET_ADDR); // Get preset selection if it exists
    if (stored < PRESET_COUNT) {
        preset_index = stored;
    }

    // Load the custom preset (index PRESET_COUNT-1), if one was ever saved.
    // An erased/unprogrammed EEPROM reads 0xFF per byte (65535 for the
    // 2-byte minutes field, 255 for the bonus byte); both fail this range
    // check, so a never-configured board keeps the {0,0} placeholder.
    uint16_t storedMinutes;
    EEPROM.get(EEPROM_CUSTOM_MINUTES_ADDR, storedMinutes);
    uint8_t storedBonus = EEPROM.read(EEPROM_CUSTOM_BONUS_ADDR);
    if (storedMinutes <= 599 && storedBonus <= 59) {
        presets[PRESET_COUNT - 1].minutes = storedMinutes;
        presets[PRESET_COUNT - 1].bonus = storedBonus;
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
                 bool plusLong, bool minusLong,                              // 3s-hold signals for + and - (acted on only while PAUSED)
                 bool minusReleased, bool minusLongFired,                    // release edge / persistent hold-fired flag for "-"
                 bool p1Now, bool prevP1, bool p2Now, bool prevP2,           // state of seesaw switches
                 Adafruit_7segment &d1, Adafruit_7segment &d2) {             // displays for player 1 and player 2

    switch (state) {
      case SELECT_DURATION:
        handleSelectDuration(plusNow, minusNow, prevPlus, prevMinus, pauseReleased, pauseLong, d1, d2);
        break;
      case PRESET_EDIT:
        handlePresetEdit(pausePressed, plusNow, prevPlus, minusNow, prevMinus, d1, d2);
        break;
      case PAUSED:
        handlePaused(pauseReleased, pauseLong, plusLong, minusLong, minusReleased, minusLongFired, d1, d2);
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
      case SHOW_MOVE_INFO:
        handleShowMoveInfo(minusReleased, d1, d2);
        break;
    }
}


