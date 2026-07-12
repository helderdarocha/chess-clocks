#include <Arduino.h>
#include "sound.h"
#include "config.h"   // BUZZER pin
#include "notes.h"    // note frequency defines used by the melody, and ZZ (rest)

void clickSound()   {
    tone(BUZZER, 2000, 40);
}
void modeSound()    {
    tone(BUZZER, 1500, 80);
}
void startSound()   {
    tone(BUZZER, 2500, 120);
}
void enterTimeSet() {
    tone(BUZZER, 1000, 120);
}
void lowBatterySound() {
    tone(BUZZER, 400, 120);
}

/* ---------- Game Over Melody ---------- */

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