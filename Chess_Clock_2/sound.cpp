#include <Arduino.h>
#include "sound.h"
#include "config.h"   // BUZZER pin
#include "notes.h"    // note frequency defines used by the melody, and ZZ (rest)

// Called when the user presses + or - to select a preset or adjust time in TIMESET. Short, high-pitched beep.
void clickSound()   {
    tone(BUZZER, 2000, 40);
}

// Called when the user enters PAUSED from RUNNING, or when the preset selection is reset. Medium-pitched beep.
void modeSound()    {
    tone(BUZZER, 1500, 80);
}

// Called when the user starts the game (enters RUNNING). Medium-high-pitched beep.
void startSound()   {
    tone(BUZZER, 2500, 120);
}

// Called when the user enters TIMESET. Medium-high-pitched beep.
void enterTimeSet() {
    tone(BUZZER, 1000, 120);
}

// Called by battery.cpp when the battery voltage is low. Single low-pitched beep.
void lowBatterySound() {
    tone(BUZZER, 400, 120);
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
// TODO: add an option in config to replace this with three long beeps, for those who find the melody annoying.
void gameOverTune() {
    for (int i = 0; i < NOTES; i++) {
        int note = pgm_read_word(&melody[i]);
        int dur  = pgm_read_word(&durations[i]);

        int duration = 1000 / dur;
        if (note != ZZ)
            tone(BUZZER, note * 4, duration);
        delay(duration * 1.3);
        noTone(BUZZER);
    }
}