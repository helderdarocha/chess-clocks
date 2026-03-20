/* Chess Clock using I2C – Version 1.1.1
 * @author Helder da Rocha
 */

#include <Arduino.h>
#include <EEPROM.h>
#include "notes.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

/* ================= Presets ================= */

struct TimePreset {
  uint16_t minutes;
  uint8_t bonus;
};

#define PRESET_COUNT 20
#define EEPROM_PRESET_ADDR 0

TimePreset presets[PRESET_COUNT] = {
  {1,0},{1,1},{2,0},{2,1},
  {3,0},{3,2},{5,0},{5,3},
  {10,0},{10,5},{15,0},{15,10},
  {30,0},{30,15},{45,0},{45,30},
  {60,0},{60,30},{90,0},{90,30}
};

uint8_t preset_index = 9;

/* ================= Hardware ================= */

#define DISPLAY_ADDRESS_1 0x70
#define DISPLAY_ADDRESS_2 0x71

#define PLY       4
#define LED1      2
#define LED2      3

#define PAUSE_BTN 8
#define PLUS_BTN  12
#define MINUS_BTN 15

#define BUZZER    11

Adafruit_7segment d1;
Adafruit_7segment d2;

/* ================= FSM ================= */

enum ClockState {
  SELECT_DURATION,
  PAUSED,
  RUNNING,
  TIMESET,
  GAME_OVER
};

ClockState state;

/* ================= Game Data ================= */

unsigned long base_time;
unsigned long bonus_time;

unsigned long player1_time;
unsigned long player2_time;

unsigned long turn_start;

bool player1_turn = true;
bool timeset_player1 = true;

/* ================= Buttons ================= */

bool prevPause = HIGH, prevPlus = HIGH, prevMinus = HIGH, prevPLY = HIGH;

#define PRESSED(n,p) ((n)==LOW && (p)==HIGH)

/* ================= Long press ================= */

unsigned long pausePressTime = 0;
bool pauseLongFired = false;

/* ================= Sounds ================= */

void clickSound()   { tone(BUZZER, 2000, 40); }
void modeSound()    { tone(BUZZER, 1500, 80); }
void startSound()   { tone(BUZZER, 2500, 120); }
void enterTimeSet() { tone(BUZZER, 1000, 120); }

/* ---------- Game Over Melody ---------- */

int NOTES = 44;
int melody[] = {
  G4, G4, G4, DS4, AS4, G4, DS4, AS4, G4,
  D5, D5, D5, DS5, AS4, FS4, DS4, AS5, G4,
  G5, G4, G4, G5, FS5, F5, E5, DS5, E5, ZZ,
  GS4, CS5, C5, B5, AS5, A5, AS5, ZZ,
  DS4, FS4, DS4, AS4, G4, DS4, AS5, G4
};

int durations[] = {
  4,4,4,6,12,4,6,12,2,
  4,4,4,6,12,4,6,12,2,
  4,6,8,4,6,8,16,16,8,8,
  8,4,6,8,16,16,8,8,
  8,4,6,8,4,6,8,2
};

void gameOverTune() {
  for (int i = 0; i < NOTES; i++) {
    int dur = 1000 / durations[i];
    if (melody[i] != ZZ)
      tone(BUZZER, melody[i] * 4, dur);
    delay(dur * 1.3);
    noTone(BUZZER);
  }
}

/* ================= Display Helpers ================= */

void updateDisplay(Adafruit_7segment &d, unsigned long ms) {
  unsigned long s = ms / 1000;
  uint8_t m = s / 60;
  uint8_t sec = s % 60;

  d.clear();
  d.writeDigitNum(0, m / 10);
  d.writeDigitNum(1, m % 10);
  d.writeDigitNum(3, sec / 10);
  d.writeDigitNum(4, sec % 10);
  d.drawColon(true);
  d.writeDisplay();
}

void showPresetSelect(uint8_t idx) {
  d1.clear();
  d1.writeDigitNum(0, idx / 10);
  d1.writeDigitNum(1, idx % 10);
  d1.drawColon(false);
  d1.writeDisplay();

  unsigned long ms =
    (presets[idx].minutes * 60UL + presets[idx].bonus) * 1000UL;
  updateDisplay(d2, ms);
}

void showPausedDisplays() {
  updateDisplay(d1, player1_time);
  updateDisplay(d2, player2_time);
}

void showTimeSetDisplay() {
  d1.clear(); d2.clear();
  if (timeset_player1)
    updateDisplay(d1, player1_time);
  else
    updateDisplay(d2, player2_time);
}

void applyPreset() {
  base_time =
    (presets[preset_index].minutes * 60UL + presets[preset_index].bonus) * 1000UL;
  bonus_time = presets[preset_index].bonus * 1000UL;

  player1_time = base_time;
  player2_time = base_time;
}

/* ================= LEDs ================= */

void updateLEDs() {
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);

  if (state == RUNNING) {
    digitalWrite(LED1, player1_turn);
    digitalWrite(LED2, !player1_turn);
  } else if (state == TIMESET) {
    digitalWrite(LED1, timeset_player1);
    digitalWrite(LED2, !timeset_player1);
  }
}

/* ================= Setup ================= */

void setup() {
  pinMode(PLY, INPUT_PULLUP);
  pinMode(PAUSE_BTN, INPUT_PULLUP);
  pinMode(PLUS_BTN, INPUT_PULLUP);
  pinMode(MINUS_BTN, INPUT_PULLUP);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  Wire.begin();
  d1.begin(DISPLAY_ADDRESS_1);
  d2.begin(DISPLAY_ADDRESS_2);

  d1.setBrightness(10);
  d2.setBrightness(10);

  uint8_t stored = EEPROM.read(EEPROM_PRESET_ADDR);
  if (stored < PRESET_COUNT) preset_index = stored;

  applyPreset();
  showPresetSelect(preset_index);

  state = SELECT_DURATION;
}

/* ================= Loop ================= */

void loop() {
  bool pauseNow = digitalRead(PAUSE_BTN);
  bool plusNow  = digitalRead(PLUS_BTN);
  bool minusNow = digitalRead(MINUS_BTN);
  bool plyNow   = digitalRead(PLY);

  bool pausePressed  = PRESSED(pauseNow, prevPause);
  bool pauseReleased = pauseNow == HIGH && prevPause == LOW;

  if (pausePressed) {
    pausePressTime = millis();
    pauseLongFired = false;
  }

  bool pauseLong = false;
  if (pauseNow == LOW && !pauseLongFired &&
      millis() - pausePressTime >= 2000) {
    pauseLong = true;
    pauseLongFired = true;
  }

  switch (state) {

    case SELECT_DURATION:
      if (PRESSED(plusNow, prevPlus)) {
        preset_index = (preset_index + 1) % PRESET_COUNT;
        EEPROM.update(EEPROM_PRESET_ADDR, preset_index);
        clickSound();
        showPresetSelect(preset_index);
      }
      if (PRESSED(minusNow, prevMinus)) {
        preset_index = (preset_index + PRESET_COUNT - 1) % PRESET_COUNT;
        EEPROM.update(EEPROM_PRESET_ADDR, preset_index);
        clickSound();
        showPresetSelect(preset_index);
      }
      if (pausePressed) {
        applyPreset();
        showPausedDisplays();
        state = PAUSED;
      }
      break;

    case PAUSED:
      if (pauseLong) {
        enterTimeSet();
        timeset_player1 = true;
        showTimeSetDisplay();
        state = TIMESET;
      }
      else if (pauseReleased) {
        player1_turn = plyNow;
        turn_start = millis();
        startSound();
        state = RUNNING;
      }
      break;

    case RUNNING: {
      unsigned long elapsed = millis() - turn_start;
      unsigned long &t = player1_turn ? player1_time : player2_time;

      if (t <= elapsed) {
        updateDisplay(player1_turn ? d1 : d2, 0);
        gameOverTune();
        state = GAME_OVER;
        break;
      }

      updateDisplay(player1_turn ? d1 : d2, t - elapsed);

      if (plyNow != player1_turn) {
        t = t - elapsed + bonus_time;
        player1_turn = !player1_turn;
        turn_start = millis();
      }

      if (pauseReleased) {
        t -= elapsed;
        showPausedDisplays();
        state = PAUSED;
      }
      break;
    }

    case TIMESET:
      if (pausePressed) {
        static uint8_t stage = 0;
        stage = (stage + 1) % 3;

        if (stage == 0) {
          showPausedDisplays();
          state = PAUSED;
        } else {
          timeset_player1 = (stage == 1);
          showTimeSetDisplay();
        }
      }

      if (PRESSED(plusNow, prevPlus)) {
        clickSound();
        if (timeset_player1) player1_time += 60000UL;
        else player2_time += 60000UL;
        showTimeSetDisplay();
      }

      if (PRESSED(minusNow, prevMinus)) {
        clickSound();
        unsigned long &t =
          timeset_player1 ? player1_time : player2_time;
        if (t >= 60000UL) t -= 60000UL;
        showTimeSetDisplay();
      }
      break;

    case GAME_OVER:
      if (pausePressed) {
        applyPreset();
        showPausedDisplays();
        state = PAUSED;
      }
      break;
  }

  updateLEDs();

  prevPause = pauseNow;
  prevPlus  = plusNow;
  prevMinus = minusNow;
  prevPLY   = plyNow;

  delay(40);
}
