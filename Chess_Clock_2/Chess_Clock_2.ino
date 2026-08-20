/* Chess Clock 2 - For Clock models CMX-2 and CMX-3
 * Version 2.1.0 (added move count and total time stats - 2026-08-19)
 * @author Helder da Rocha
 */

#include <Arduino.h>

#include "config.h"     // per-unit hardware configuration (battery, display, pins)
#include "battery.h"    // battery voltage reading and low-battery warnings
#include "sound.h"      // buzzer tones and game-over melody
#include "display.h"    // 7-segment display rendering
#include "power.h"      // sleep mode for power saving
#include "game.h"       // game state machine and button handling, presets, seesaw switch macros
#include "rtc.h"        // optional DS3231: corrects millis() drift, no config needed

#include <Wire.h>                 // i2C support
#include <Adafruit_GFX.h>         // Displays
#include "Adafruit_LEDBackpack.h" // Displays

/* ================= Hardware ================= */
// Pins, display addresses, and brightness levels now live in config.h

// Displays
Adafruit_7segment d1;
Adafruit_7segment d2;

/* ================= Power saving and battery warning variables ================= */
// Battery/idle thresholds now live in config.h

unsigned long lastActivityMs = 0;   // updated on every button press
unsigned long lastRunningMs = 0;    // updated when the clock is RUNNING (for idle timeout)
bool sleeping = false;
bool justWokeUp = false;
bool awaitingButtonRelease = false;

/* ================= Control buttons ================= */
// Pause + select + increment/decrement
bool prevPause = HIGH,
prevPlus = HIGH,
prevMinus = HIGH;

// Game latch
bool prevP1 = HIGH,
prevP2 = HIGH;

// To control increments and pausing using PAUSE button
unsigned long pausePressTime = 0;
bool pauseLongFired = false;

// To reset using + and - buttons
unsigned long resetPressTime = 0;
bool resetLongFired = false;

// To toggle sound on/off using + and - buttons (held 3s while PAUSED)
unsigned long plusPressTime = 0;
bool plusLongFired = false;
unsigned long minusPressTime = 0;
bool minusLongFired = false;

//////////////////// FUNCTIONS ////////////////////////////

// Battery reading/warning functions live in battery.h/battery.cpp
// Sleep functions live in power.h/power.cpp
// Sound functions and the melody data live in sound.h/sound.cpp
// Display rendering functions live in display.h/display.cpp
// Presets, game/turn state, and the state-machine handlers live in game.h/game.cpp

//////////// ARDUINO CONTROL - setup() and loop() //////////////////

/* ================= SETUP ================= */

void setup() {
    // Player lever
    pinMode(PLY1, INPUT_PULLUP);
    pinMode(PLY2, INPUT_PULLUP);

    // Pause LED
    pinMode(LED_PAUSE, OUTPUT);
    digitalWrite(LED_PAUSE, LOW);

    // Control buttons
    pinMode(PAUSE_BTN, INPUT_PULLUP);
    pinMode(PLUS_BTN, INPUT_PULLUP);
    pinMode(MINUS_BTN, INPUT_PULLUP);

    // Buzzer
    pinMode(BUZZER, OUTPUT);

    // Displays
    Wire.begin();
    d1.begin(DISPLAY_ADDRESS_1);
    d2.begin(DISPLAY_ADDRESS_2);
    d1.setBrightness(DISPLAY_ON);
    d2.setBrightness(DISPLAY_ON);

    // Warn if battery is low - beep even if sound is off!
    checkBatteryAtStartup(d1, d2);

    // Load saved sound on/off preference after alerting about battery, so the user can hear the warning even if sound was off last time.
    soundInit();

    // Detect an optional DS3231; falls back to plain millis() if absent
    rtcInit();

    // Load persisted preset, apply it, draw preset-selection screen
    clockInit(d1, d2);

    noTone(BUZZER);
}

/* ================= LOOP ================= */
void loop() {
    // Read button states
    bool pauseNow = digitalRead(PAUSE_BTN);
    bool plusNow = digitalRead(PLUS_BTN);
    bool minusNow = digitalRead(MINUS_BTN);
    bool p1Now = digitalRead(PLY1);
    bool p2Now = digitalRead(PLY2);

    bool pausePressed = PRESSED(pauseNow, prevPause);
    bool pauseReleased = (pauseNow == HIGH && prevPause == LOW);

    // Pause was pressed
    if (pausePressed) {
        pausePressTime = millis();
        pauseLongFired = false;
    }

    // Long pause was pressed - to increment or decrement user time
    bool pauseLong = false;
    if (pauseNow == LOW && !pauseLongFired && millis() - pausePressTime >= 2000) {
        pauseLong = true;
        pauseLongFired = true;
    }

    // + and - buttons were pressed to reset the clock
    bool resetCombo = (plusNow == LOW && minusNow == LOW);
    if (resetCombo && !resetLongFired && resetPressTime == 0) {
        resetPressTime = millis();
    }
    if (!resetCombo) {
        resetPressTime = 0;
        resetLongFired = false;
    }
    bool resetLong = false;
    if (resetCombo && !resetLongFired && resetPressTime > 0 && millis() - resetPressTime >= 1000) {
        resetLong = true;
        resetLongFired = true;
    }

    // + or - held alone (not the reset combo) for 3s while PAUSED toggles sound on/off.
    // Tracked unconditionally here, like the other button timers; game.cpp only acts
    // on these while in the PAUSED state.
    if (PRESSED(plusNow, prevPlus)) {
        plusPressTime = millis();
        plusLongFired = false;
    }
        bool plusLong = false;
        if (plusNow == LOW && !plusLongFired && millis() - plusPressTime >= 3000) {
        plusLong = true;
        plusLongFired = true;
    }

        if (PRESSED(minusNow, prevMinus)) {
        minusPressTime = millis();
        minusLongFired = false;
    }
        bool minusLong = false;
        if (minusNow == LOW && !minusLongFired && millis() - minusPressTime >= 3000) {
        minusLong = true;
        minusLongFired = true;
    }

    // Release edge for "-", used while PAUSED to open the move-info screen
    // (short tap) without conflicting with the 3s hold-to-mute above (see
    // minusLongFired passed alongside it into clockUpdate()).
    bool minusReleased = (minusNow == HIGH && prevMinus == LOW);


    // A button was pressed to wake up the clock after sleep mode (pause, plus/minus or player levers)
    if (pausePressed || PRESSED(plusNow, prevPlus)
                     || PRESSED(minusNow, prevMinus)
                     || PRESSED(p1Now, prevP1)
                     || PRESSED(p2Now, prevP2)) {
        lastActivityMs = millis();
    }

    rtcUpdate();  // throttled internally; no-op if no RTC was found

    if (resetLong && !isRunning()) {
        resetToSelectDuration(d1, d2);
    }

    if (isRunning()) {
        lastRunningMs = millis();  // used to calculate idle timeout
    }

    // will sleep if idle for too long and not running (wakes up on any button press)
    // if the clock was running, and just changed state automatically (game over), it will wait IDLE_TIMEOUT_MS
    // before sleeping, to give the user a chance to see the result and hear the game over tune.
    if (!sleeping && !isRunning()
                  && millis() - lastActivityMs > IDLE_TIMEOUT_MS
                  && millis() - lastRunningMs > IDLE_TIMEOUT_MS) {
        goToSleep(d1, d2, sleeping, justWokeUp, lastActivityMs,
                  isSelectingDuration(), getPresetIndex(), getPresetTimeMs(),
                  getPlayer1Time(), getPlayer2Time());
    }

    if (justWokeUp) {
        justWokeUp = false;
        rtcNotifyWake();    // necessary because RTC is powered independently.
        awaitingButtonRelease = true;  // swallow whichever button(s) caused the wake
    } else if (awaitingButtonRelease) { // Woke up, but button may not have been released yet
        // Ignore all button (not lever) activity until everything is released, so the
        // press that woke the clock — and its eventual release — can't
        // also be interpreted as a deliberate press by the state machine.
        if (pauseNow == HIGH && plusNow == HIGH && minusNow == HIGH) {
            awaitingButtonRelease = false;
        }
    } else { 
        clockUpdate(plusNow, prevPlus, minusNow, prevMinus,
                    pausePressed, pauseReleased,
                    pauseLong, pauseLongFired,
                    plusLong, minusLong,
                    minusReleased, minusLongFired,
                    p1Now, prevP1, p2Now, prevP2,
                    d1, d2);
    }

    // PAUSE LED: on while PAUSED or showing the move-info screen (which is
    // just a brief detour off PAUSED, not a real gameplay state), off in
    // every other state and while sleeping (goToSleep() also turns it off
    // before it blocks, since this line won't run again until after the
    // clock wakes).
    digitalWrite(LED_PAUSE, ((isPaused() || isShowingMoveInfo()) && !sleeping) ? HIGH : LOW);

    if (pauseNow == HIGH) {
        pauseLongFired = false;
    }
    if (plusNow == HIGH) {
        plusLongFired = false;
    }
    if (minusNow == HIGH) {
        minusLongFired = false;
    }

    prevPause = pauseNow;
    prevPlus = plusNow;
    prevMinus = minusNow;
    prevP1 = p1Now;
    prevP2 = p2Now;

    delay(40);

    noTone(BUZZER);
}