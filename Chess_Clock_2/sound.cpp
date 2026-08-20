#include <Arduino.h>
#include <EEPROM.h>   // persists the sound on/off preference
#include "sound.h"
#include "config.h"   // BUZZER pin
#include "notes.h"    // note frequency defines used by the melody, and ZZ (rest)

// EEPROM address for the sound on/off preference. Address 0 is already
// used by game.cpp for the preset index; this module owns address 1 and
// touches no other address, so the two modules' EEPROM use can't collide.
#define EEPROM_SOUND_ADDR 1

// True if sounds are currently enabled. Kept in RAM; EEPROM is only read
// once (soundInit()) and written when the value actually changes.
static bool soundEnabled = true;

// Loads the saved preference. An erased EEPROM cell reads 0xFF, which (like
// any value other than 0) is treated as "on", so a fresh board defaults to
// sound enabled, matching the previous (non-configurable) behavior.
void soundInit() {
    soundEnabled = (EEPROM.read(EEPROM_SOUND_ADDR) != 0);
}

bool isSoundEnabled() {
    return soundEnabled;
}

void setSoundEnabled(bool enabled) {
    if (enabled == soundEnabled)
        return;   // already in the requested state - nothing to change or save
    soundEnabled = enabled;
    EEPROM.update(EEPROM_SOUND_ADDR, enabled ? 1 : 0);
}

/* ---------- Sounds ---------- */

// Called when the user presses + or - to select a preset or adjust time in TIMESET. Short, high-pitched beep.
void clickSound()   {
    if (!soundEnabled) return;
    tone(BUZZER, 2000, 40);
}

// Called when the user enters PAUSED from RUNNING, or when the preset selection is reset. Medium-pitched beep.
void modeSound()    {
    if (!soundEnabled) return;
    tone(BUZZER, 1500, 80);
}

// Called when the user starts the game (enters RUNNING). Medium-high-pitched beep.
void startSound()   {
    if (!soundEnabled) return;
    tone(BUZZER, 2500, 120);
}

// Called when the user enters TIMESET. Medium-high-pitched beep.
void enterTimeSet() {
    if (!soundEnabled) return;
    tone(BUZZER, 1000, 120);
}

// Called by battery.cpp when the battery voltage is low. Single low-pitched beep.
// This warning beeps even if sounds are disabled, so the user is alerted to the low battery even in tournament mode.
void lowBatterySound() {
    // if (!soundEnabled) return;  // Uncomment for strict no-sound rule
    tone(BUZZER, 400, 120);
}

// Called right after sound is turned back on (holding + for 3s while PAUSED).
// Doesn't check soundEnabled: the caller only calls this once sound is
// already back on, and it exists specifically to be audible at that moment.
void soundOnBeep() {
    tone(BUZZER, 2000, 150);
}

/* ---------- Game Over Melody ---------- */
// The melody is stored in PROGMEM to save RAM.
// Each note is defined in notes.h, and ZZ represents a rest (no sound).

static const int NOTES = 44;

static const int melody[] PROGMEM = {
    G4, G4, G4, DS4, AS4, G4, DS4, AS4, G4,
    D5, D5, D5, DS5, AS4, FS4, DS4, AS5, G4,
    G5, G4, G4, G5, FS5, F5, E5, DS5, E5, ZZ,
    GS4, CS5, C5, B5, AS5, A5, AS5, ZZ,
    DS4, FS4, DS4, AS4, G4, DS4, AS5, G4
  };

static const int durations[] PROGMEM = {
    4,4,4,6,12,4,6,12,2,
    4,4,4,6,12,4,6,12,2,
    4,6,8,4,6,8,16,16,8,8,
    8,4,6,8,16,16,8,8,
    8,4,6,8,4,6,8,2
  };

// Plays when the game is over (when a player's time runs out). Blocking — takes a few seconds.
// Set GAME_OVER_SOUND in config.h to GAME_OVER_BEEPS for three long beeps instead of the melody.
void gameOverTune() {
    if (!soundEnabled) return;

#if GAME_OVER_SOUND == GAME_OVER_BEEPS
    for (int i = 0; i < 3; i++) {
        tone(BUZZER, 440, 500);
        delay(650);
        noTone(BUZZER);
        delay(200);
    }
#else
    for (int i = 0; i < NOTES; i++) {
        int note = pgm_read_word(&melody[i]);
        int dur  = pgm_read_word(&durations[i]);

        int duration = 1000 / dur;
        if (note != ZZ)
            tone(BUZZER, note * 4, duration);
        delay(duration * 1.3);
        noTone(BUZZER);
    }
#endif
}