/* Chess Clock using I2C – Version 1.5.6 - For Clock models 2 and 3.
 * New clocks should use Version 2 (see Chess_Clock_2 folder).
 *
 * @author Helder da Rocha
 */

#include <Arduino.h>
#include <EEPROM.h>     // EEPROM support to save current preset selection
#include <limits.h>     // just for ULONG_MAX (0xFFFFFFFFUL) definition
#include <avr/sleep.h>  // to reduce power consumption if user forgets to turn off
#include <avr/power.h>  // additional power controls to save energy during sleep
#include "notes.h"      // notes for the game over tune

#include <Wire.h>                 // i2C support
#include <Adafruit_GFX.h>         // Displays
#include "Adafruit_LEDBackpack.h" // Displays

/* ================= Presets ================= */
// Presets for BULLET, BLITZ, RAPID and CLASSICAL (to 90 minutes)
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
uint8_t timesetStage = 0;

/* ================= Hardware ================= */

#define DISPLAY_ADDRESS_1 0x70
#define DISPLAY_ADDRESS_2 0x71

// Player buttons
#define PLY1      4
#define PLY2      5

// Control buttons
#define PAUSE_BTN 8
#define PLUS_BTN  12
#define MINUS_BTN 15

#define BUZZER    11

// Displays
Adafruit_7segment d1;
Adafruit_7segment d2;

// Display levels
#define DISPLAY_OFF 0
#define DISPLAY_DIM 5
#define DISPLAY_STD 7   // Use for white led
#define DISPLAY_MAX 10  // Use for blue led
#define DISPLAY_OVR 15

/* ================= Power saving and battery warning variables ================= */

// Low battery loop - called once per minute during RUNNING and PAUSED.
// Considering 3.35V in battery + Considering avg 1.1 AREF and current circuit with IRF9530 + Schottky diode
// #define BATT_WARN_MV    2950UL   // best for 3 AA batteries (NiMH 4.5V or Alkakine 3.6V)
#define BATT_WARN_MV    3050UL   // best for internal Molicell M35A 3.7V LiOn battery (~2.7V on 5V pin at minimum)
#define BATT_CHECK_MS   60000UL  // check every 60 seconds

// Protection to reduce battery use when idle (not RUNNING)
#define IDLE_TIMEOUT_MS  300000UL   // 5 minutes with no interaction and not RUNNING

unsigned long lastActivityMs = 0;   // updated on every button press
unsigned long lastRunningMs = 0;    // updated when the clock is RUNNING (for idle timeout)
unsigned long lastBattCheckMs = 0;
bool sleeping = false;

/* ================= Game control variables ================= */

// Macro to detect a button press edge (HIGH -> LOW) with INPUT_PULLUP
// Necessary to avoid triggering multiple times during the loop while the button is held down
#define PRESSED(n,p) ((n)==LOW && (p)==HIGH)

// Macro to detect released edge - necessary to deal with inconsistent states (two released buttons)
#define RELEASED(n,p) ((n)==HIGH && (p)==LOW)

unsigned long base_time;
unsigned long bonus_time;
unsigned long player1_time;
unsigned long player2_time;
unsigned long turn_start_time;
unsigned long turn_start;

bool player1_turn = false;
bool timeset_player1 = true;
bool running_player1;
bool gameOverPlayed = false;
bool justWokeUp = false;

// To avoid excessive display updates
unsigned long lastDrawnSeconds1 = ULONG_MAX;
unsigned long lastDrawnSeconds2 = ULONG_MAX;

// Include bonus in first turn (hardwired)
bool includeBonusInFirstTurn = true; // if false, 10:05 will start at 10:00 and increment bonus after

/* ================= Control buttons ================= */
// Pause + select + increment/decrement
bool prevPause = HIGH,
prevPlus = HIGH,
prevMinus = HIGH;

// Game latch
bool prevP1 = HIGH,
prevP2 = HIGH;

bool ignorePauseRelease = false;

// To control increments and pausing using PAUSE button
unsigned long pausePressTime = 0;
bool pauseLongFired = false;

// To reset using + and - buttons
unsigned long resetPressTime = 0;
bool resetLongFired = false;

/* ================= Finite-state machine ================= */
// Clock states
enum ClockState {
  SELECT_DURATION,
  PAUSED,
  RUNNING,
  TIMESET,
  GAME_OVER
};

ClockState state;


//////////////////// FUNCTIONS ////////////////////////////

/* ===== Power saving and battery monitor functions ======= */

// Battery monitor: select internal 1.1V reference, measure against VCC
long readVCC_mV() {
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);                  // let reference settle
  ADCSRA |= _BV(ADSC);       // start conversion
  while (bit_is_set(ADCSRA, ADSC));  // wait
  // VCC (mV) = 1100 * 1024 / ADC
  return 1125300L / ADC;     // 1100 * 1023 ≈ 1125300
}

// Three beeps every minute if battery is low
void lowBatteryBeep() {
  for (int i = 0; i < 3; i++) {
  lowBatterySound();
  delay(220);
}
}

void checkBattery() {
  unsigned long now = millis();
  if (now - lastBattCheckMs < BATT_CHECK_MS) return;
  lastBattCheckMs = now;
  if (readVCC_mV() < BATT_WARN_MV)
  lowBatteryBeep();
}

// Show warning at startup
void showBatteryWarning() {
  for (int i = 0; i < 2; i++) {
  // "BAtt" on display 1, "Lo--" on display 2
  d1.clear();
  d1.writeDigitRaw(0, 0x7C);  // b
  d1.writeDigitRaw(1, 0x77);  // A
  d1.writeDigitRaw(3, 0x78);  // t
  d1.writeDigitRaw(4, 0x78);  // t
  d1.drawColon(false);
  d1.writeDisplay();

  d2.clear();
  d2.writeDigitRaw(0, 0x38);  // L
  d2.writeDigitRaw(1, 0x5C);  // o
  d2.writeDigitRaw(3, 0x40);  // -
  d2.writeDigitRaw(4, 0x40);  // -
  d2.drawColon(false);
  d2.writeDisplay();

  lowBatteryBeep();            // beep while showing the warning

  // blank both displays between flashes
  d1.clear(); d1.writeDisplay();
  d2.clear(); d2.writeDisplay();
  delay(300);
}
}

// Tries to reduce battery use by turning off everything that is possible (when not RUNNING)
// See https://forum.arduino.cc/t/what-to-turn-off-and-how/79151/9
void goToSleep() {
  // Blank both displays
  d1.setBrightness(DISPLAY_OFF); d1.clear(); d1.writeDisplay();
  d2.setBrightness(DISPLAY_OFF); d2.clear(); d2.writeDisplay();
  sleeping = true;

  // Enable pin-change interrupts on all five buttons so any one wakes the CPU.
  // PLY1=4(PD4) PLY2=5(PD5) PAUSE=8(PB0) PLUS=12(PB4) MINUS=15(PC1)
  PCICR  |= _BV(PCIE0);   // enable PCINT[7:0]  (port B — PAUSE, PLUS)
  PCICR  |= _BV(PCIE1);   // enable PCINT[14:8] (port C — MINUS)
  PCICR  |= _BV(PCIE2);   // enable PCINT[23:16](port D — PLY1, PLY2)
  PCMSK0 |= _BV(PCINT0) | _BV(PCINT4);   // PB0=PAUSE, PB4=PLUS
  PCMSK1 |= _BV(PCINT9);                 // PC1=MINUS
  PCMSK2 |= _BV(PCINT20) | _BV(PCINT21); // PD4=PLY1, PD5=PLY2

  power_adc_disable();   // saves ~0.3mA
  power_spi_disable();   // not sure about this, but disabling it anyway
  power_usart0_disable(); // not sure about this, but disabling it anyway

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();              // halts here until interrupt fires

  // --- wakwe up here after any button press ---
  sleep_disable();

  // Disable pin-change interrupts again
  PCICR  &= ~(_BV(PCIE0) | _BV(PCIE1) | _BV(PCIE2));

  power_adc_enable(); // enable after waking up
  power_spi_enable();
  power_usart0_enable();

  // Restore displays
  d1.setBrightness(DISPLAY_DIM); d2.setBrightness(DISPLAY_DIM);
  sleeping = false;
  justWokeUp = true;
  lastActivityMs = millis();

  // Redraw whatever was showing before sleep
  if (state == SELECT_DURATION) {
  showPresetSelect(preset_index);
} else {
  showPausedDisplays();
}
}

// Empty ISRs needed to wake the CPU
ISR(PCINT0_vect) {}
ISR(PCINT1_vect) {}
ISR(PCINT2_vect) {}

/* ================= Sounds ================= */

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

int NOTES = 44;
const int melody[] PROGMEM = {
G4, G4, G4, DS4, AS4, G4, DS4, AS4, G4,
D5, D5, D5, DS5, AS4, FS4, DS4, AS5, G4,
G5, G4, G4, G5, FS5, F5, E5, DS5, E5, ZZ,
GS4, CS5, C5, B5, AS5, A5, AS5, ZZ,
DS4, FS4, DS4, AS4, G4, DS4, AS5, G4
};

const int durations[] PROGMEM = {
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

/* ================= Game helper functions ================= */

bool isPlayer1Turn() {
  return digitalRead(PLY2) == LOW;
}

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

// Show selection for preset
void showPresetSelect(uint8_t idx) {
  d1.clear();
  d1.writeDigitNum(0, idx / 10);
  d1.writeDigitNum(1, idx % 10);
  d1.drawColon(false);
  d1.writeDisplay();

  unsigned long ms = (presets[idx].minutes * 60UL + presets[idx].bonus) * 1000UL;
  updateDisplay(d2, ms);
}

void showPausedDisplays() {
  updateDisplay(d1, player1_time);
  updateDisplay(d2, player2_time);
}

// Set the time for each player
void showTimeSetDisplay() {
  if (timesetStage == 0) {
  // Editing Player 1
  updateDisplay(d1, player1_time);

  d2.clear();
  d2.drawColon(false);
  d2.writeDisplay();
}
  else {
  // Editing Player 2
  updateDisplay(d2, player2_time);

  d1.clear();
  d1.drawColon(false);
  d1.writeDisplay();
}
}

// Apply preset before starting game
void applyPreset() {
  base_time = (presets[preset_index].minutes * 60UL) * 1000UL;
  bonus_time = presets[preset_index].bonus * 1000UL;

  player1_time = base_time + (includeBonusInFirstTurn ? bonus_time : 0);
  player2_time = base_time + (includeBonusInFirstTurn ? bonus_time : 0);
}

/* ================= State functions - called during LOOP in switch ================= */
// SELECT_DURATION
void handleSelectDuration(bool plusNow, bool minusNow,
bool prevPlus, bool prevMinus,
bool pauseReleased) {
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
  if (pauseReleased) {
  applyPreset();
  showPausedDisplays();
  state = PAUSED;
}
}

// PAUSED
void handlePaused(bool pauseReleased, bool pauseLong) {
  checkBattery();
  if (ignorePauseRelease) {
  if (pauseReleased) ignorePauseRelease = false;
  return;
}
  if (pauseLong) {
  enterTimeSet();
  timesetStage = 0;
  showTimeSetDisplay();
  state = TIMESET;
} else if (pauseReleased) {
  player1_turn    = isPlayer1Turn();
  turn_start_time = millis();
  startSound();
  state = RUNNING;
}
}

// RUNNING
void handleRunning(bool pauseReleased, bool pauseLongFired,
bool p1Now, bool p2Now,
bool prevP1, bool prevP2) {
  checkBattery();
  unsigned long elapsed = millis() - turn_start_time;

  unsigned long       *activeTime    = player1_turn ? &player1_time    : &player2_time;
  Adafruit_7segment   *activeDisplay = player1_turn ? &d1              : &d2;
  unsigned long       *tracker       = player1_turn ? &lastDrawnSeconds1 : &lastDrawnSeconds2;

  if (*activeTime > elapsed) {
  unsigned long remaining = *activeTime - elapsed;
  unsigned long secs      = remaining / 1000;
  if (secs != *tracker) {
  updateDisplay(*activeDisplay, remaining);
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
  *activeTime  = (*activeTime > elapsed) ? *activeTime - elapsed : 0;
  *activeTime += bonus_time;
  lastDrawnSeconds1 = ULONG_MAX;
  lastDrawnSeconds2 = ULONG_MAX;
  updateDisplay(*activeDisplay, *activeTime);

  player1_turn = !player1_turn;
  turn_start_time = millis();

  unsigned long *otherTime = player1_turn ? &player1_time : &player2_time;
  Adafruit_7segment *otherDisplay = player1_turn ? &d1 : &d2;
  updateDisplay(*otherDisplay, *otherTime);
}

  if (pauseReleased && !pauseLongFired) {
  *activeTime = (*activeTime > elapsed) ? *activeTime - elapsed : 0;
  lastDrawnSeconds1 = ULONG_MAX;
  lastDrawnSeconds2 = ULONG_MAX;
  updateDisplay(*activeDisplay, *activeTime);
  modeSound();
  state = PAUSED;
}
}

// TIMESET
void handleTimeset(bool pausePressed,
bool plusNow,  bool prevPlus,
bool minusNow, bool prevMinus) {
  if (pausePressed) {
  timesetStage++;
  if (timesetStage > 1) {
  showPausedDisplays();
  ignorePauseRelease = true;
  state = PAUSED;
} else {
  showTimeSetDisplay();
}
}
  if (PRESSED(plusNow, prevPlus)) {
  clickSound();
  if (timesetStage == 0) player1_time += 60000UL;
  else                   player2_time += 60000UL;
  showTimeSetDisplay();
}
  if (PRESSED(minusNow, prevMinus)) {
  clickSound();
  unsigned long &t = (timesetStage == 0) ? player1_time : player2_time;
  if (t >= 60000UL) t -= 60000UL;
  showTimeSetDisplay();
}
}

// GAME_OVER
void handleGameOver(bool pauseReleased) {
  if (!gameOverPlayed) {
  gameOverTune();
  gameOverPlayed = true;
}
  if (pauseReleased) {
  applyPreset();
  showPausedDisplays();
  state = PAUSED;
}
}

//////////// ARDUINO CONTROL - setup() and loop() //////////////////

/* ================= SETUP ================= */

void setup() {
  // Player lever
  pinMode(PLY1, INPUT_PULLUP); // I am also using a 10k pull-up (for a lower resistance)
  pinMode(PLY2, INPUT_PULLUP);

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
  d1.setBrightness(DISPLAY_DIM);
  d2.setBrightness(DISPLAY_DIM);

  // Warn if battery is low!
  if (readVCC_mV() < BATT_WARN_MV) showBatteryWarning();

  // Get preset selection if it exists
  uint8_t stored = EEPROM.read(EEPROM_PRESET_ADDR);
  if (stored < PRESET_COUNT) preset_index = stored;

  applyPreset();
  showPresetSelect(preset_index);
  state = SELECT_DURATION;

  noTone(BUZZER);
}

/* ================= LOOP ================= */
void loop() {
  // Read button states
  bool pauseNow  = digitalRead(PAUSE_BTN);
  bool plusNow   = digitalRead(PLUS_BTN);
  bool minusNow  = digitalRead(MINUS_BTN);
  bool p1Now     = digitalRead(PLY1);
  bool p2Now     = digitalRead(PLY2);

  bool pausePressed  = PRESSED(pauseNow, prevPause);
  bool pauseReleased = (pauseNow == HIGH && prevPause == LOW);

  // Pause was pressed
  if (pausePressed) {
  pausePressTime = millis();
  pauseLongFired = false;
}

  // Long pause was pressed - to increment or decrement user time
  bool pauseLong = false;
  if (pauseNow == LOW && !pauseLongFired
  && millis() - pausePressTime >= 2000) {
  pauseLong      = true;
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
  if (resetCombo && !resetLongFired
  && resetPressTime > 0
  && millis() - resetPressTime >= 1000) {
  resetLong      = true;
  resetLongFired = true;
}

  // A button was pressed to wake up the clock after sleep mode (pause, plus/minus or player levers)
  if (pausePressed || PRESSED(plusNow, prevPlus)
  || PRESSED(minusNow, prevMinus)
  || PRESSED(p1Now, prevP1)
  || PRESSED(p2Now, prevP2)) {
  lastActivityMs = millis();
}

  if (resetLong && state != RUNNING) {
  applyPreset();
  showPresetSelect(preset_index);
  modeSound();
  state = SELECT_DURATION;
}

  if (state == RUNNING) {
  lastRunningMs = millis();  // used to calculate idle timeout
}

  // will sleep if idle for too long and not running (wakes up on any button press)
  // if the clock was running, and just changed state automatically (game over), it will wait IDLE_TIMEOUT_MS
  // before sleeping, to give the user a chance to see the result and hear the game over tune.
  if (!sleeping && state != RUNNING
  && millis() - lastActivityMs > IDLE_TIMEOUT_MS
  && millis() - lastRunningMs > IDLE_TIMEOUT_MS) {
  goToSleep();
}

  if (justWokeUp) {
  justWokeUp = false;
} else {
  switch (state) {
  case SELECT_DURATION:
  handleSelectDuration(plusNow, minusNow, prevPlus, prevMinus, pauseReleased);
  break;
  case PAUSED:
  handlePaused(pauseReleased, pauseLong);
  break;
  case RUNNING:
  handleRunning(pauseReleased, pauseLongFired, p1Now, p2Now, prevP1, prevP2);
  break;
  case TIMESET:
  handleTimeset(pausePressed, plusNow, prevPlus, minusNow, prevMinus);
  break;
  case GAME_OVER:
  handleGameOver(pauseReleased);
  break;
}
}

  if (pauseNow == HIGH) pauseLongFired = false;

  prevPause = pauseNow;
  prevPlus  = plusNow;
  prevMinus = minusNow;
  prevP1    = p1Now;
  prevP2    = p2Now;

  delay(40);
  noTone(BUZZER);
}