#include <Arduino.h>
#include "display.h"

// Time is displayed as MM:SS for times less than 100 minutes, and as H:MM
// for times 100 minutes or more. The bonus is added to the player's time after each turn.
// When displaying H:MM the colon blinks every half second to indicate that the clock is running.
// The display is updated every second, but the time is decremented continuously in the background,
// so the display may be slightly behind the actual time (fractions of a second).
// The display is also updated when the user presses the + or - buttons to set the time in TIMESET mode.

// Above this, MM:SS's two-digit minute field would overflow (max 99:59),
// so we switch to H:MM. Below it, MM:SS gives full second-by-second
// ticking feedback, which matters for realistic games — e.g. a 90:30
// preset should visibly tick as "90:30", "89:15", etc., not lose that
// granularity just because it's a "classical" time control.
#define HOUR_FORMAT_THRESHOLD_MS  (100UL * 60UL * 1000UL)   // 100:00

// Hysteresis band for the continuously-ticking countdown only. Without
// this, a bonus repeatedly nudging the value across the 99/100-minute
// line would flicker the format on back-to-back turns.
#define HOUR_FORMAT_ENTER_MS      (100UL * 60UL * 1000UL)   // switch to H:MM at >= 100:00
#define HOUR_FORMAT_EXIT_MS       (98UL  * 60UL * 1000UL)   // switch back to MM:SS below 98:00

// Largest value H:MM can show with a single hour digit (0-9); anything
// beyond this is clamped defensively rather than drawing a garbled digit.
// The max displayable time is 9:59, which is 9 hours and 59 minutes in milliseconds.
#define MAX_DISPLAYABLE_MS        (9UL * 3600UL * 1000UL + 59UL * 60UL * 1000UL)  // 9:59

// MM:SS — digits 0/1 hold the minute, digits 3/4 hold the second, colon is always on.
static void writeMMSS(Adafruit_7segment &d, unsigned long ms) {
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

// H:MM — digit 0 left blank (only one hour digit is available/needed up
// to the 9:59 clamp), digit 1 holds the hour, digits 3/4 hold the minute.
static void writeHMM(Adafruit_7segment &d, unsigned long ms, bool colonOn) {
    unsigned long totalMinutes = ms / 60000UL;
    uint8_t hours   = totalMinutes / 60;
    uint8_t minutes = totalMinutes % 60;

    d.clear();
    d.writeDigitNum(1, hours);
    d.writeDigitNum(3, minutes / 10);
    d.writeDigitNum(4, minutes % 10);
    d.drawColon(colonOn);
    d.writeDisplay();
}

// Updates the display with the given time in milliseconds, using MM:SS or H:MM format as appropriate.
// This is used for the PAUSED and TIMESET states, where the time is not continuously ticking down.
void updateDisplay(Adafruit_7segment &d, unsigned long ms) {
    if (ms > MAX_DISPLAYABLE_MS)
        ms = MAX_DISPLAYABLE_MS;

    if (ms >= HOUR_FORMAT_THRESHOLD_MS) {
        writeHMM(d, ms, true);
    } else {
        writeMMSS(d, ms);
    }
}

// Updates the display with the given time in milliseconds, using MM:SS or H:MM format as appropriate.
// This is used for the RUNNING state, where the time is continuously ticking down.
// The colon blinks every half second in H:MM format to indicate that the clock is running.
// The function returns true if the display is in H:MM format, and false if it is in MM:SS format.
// The wasHourFormat parameter is used to determine whether to stay in H:MM format or switch back to
// MM:SS format based on the hysteresis thresholds.
bool updateRunningDisplay(Adafruit_7segment &d, unsigned long ms, bool wasHourFormat) {
    if (ms > MAX_DISPLAYABLE_MS)
        ms = MAX_DISPLAYABLE_MS;

    bool hourFormat = wasHourFormat
                       ? (ms >= HOUR_FORMAT_EXIT_MS)    // stay in H:MM until below the exit line
                       : (ms >= HOUR_FORMAT_ENTER_MS);  // only enter H:MM once above the enter line

    if (hourFormat) {
        bool colonOn = ((ms / 500UL) % 2UL) == 0UL;  // blink colon every half second
        writeHMM(d, ms, colonOn);
    } else {
        writeMMSS(d, ms);
    }
    return hourFormat;
}

// Blanks a display with the colon off (used between digits during TIMESET).
static void clearDisplay(Adafruit_7segment &d) {
    d.clear();
    d.drawColon(false);
    d.writeDisplay();
}

void showPresetSelect(uint8_t idx, unsigned long presetMs,
                      Adafruit_7segment &d1, Adafruit_7segment &d2) {
    d1.clear();
    d1.writeDigitNum(0, idx / 10);
    d1.writeDigitNum(1, idx % 10);
    d1.drawColon(false);
    d1.writeDisplay();

    updateDisplay(d2, presetMs);
}

void showPausedDisplays(Adafruit_7segment &d1, unsigned long time1,
                        Adafruit_7segment &d2, unsigned long time2) {
    updateDisplay(d1, time1);
    updateDisplay(d2, time2);
}

void showTimeSetDisplay(uint8_t stage,
                        Adafruit_7segment &d1, unsigned long time1,
                        Adafruit_7segment &d2, unsigned long time2) {
    if (stage == 0) {
        // Editing Player 1
        updateDisplay(d1, time1);
        clearDisplay(d2);
    } else {
        // Editing Player 2
        updateDisplay(d2, time2);
        clearDisplay(d1);
    }
}

// Raw segment codes for letters not in the display library's built-in
// digit font (bit0=a, bit1=b, bit2=c, bit3=d, bit4=e, bit5=f, bit6=g).
#define SEG_S 0x6D  // looks like digit 5, same shape
#define SEG_n 0x54
#define SEG_d 0x5E
#define SEG_F 0x71

void showSoundStatus(bool enabled, Adafruit_7segment &d1, Adafruit_7segment &d2) {
    // d1: "Snd" (right-aligned, digit 0 left blank)
    d1.clear();
    d1.writeDigitRaw(1, SEG_S);
    d1.writeDigitRaw(3, SEG_n);
    d1.writeDigitRaw(4, SEG_d);
    d1.drawColon(false);
    d1.writeDisplay();

    // d2: "0n" or "0ff"
    d2.clear();
    if (enabled) {
        d2.writeDigitNum(3, 0);
        d2.writeDigitRaw(4, SEG_n);
    } else {
        d2.writeDigitNum(0, 0);
        d2.writeDigitRaw(1, SEG_F);
        d2.writeDigitRaw(3, SEG_F);
    }
    d2.drawColon(false);
    d2.writeDisplay();
}

void showPresetEditDisplay(uint8_t field, uint8_t hours, uint8_t minTens, uint8_t minOnes,
                           uint8_t bonusTens, uint8_t bonusOnes, bool blinkVisible,
                           Adafruit_7segment &d1, Adafruit_7segment &d2) {
    d1.clear();
    if (field != PRESET_EDIT_HOURS    || blinkVisible) d1.writeDigitNum(1, hours);
    if (field != PRESET_EDIT_MIN_TENS || blinkVisible) d1.writeDigitNum(3, minTens);
    if (field != PRESET_EDIT_MIN_ONES || blinkVisible) d1.writeDigitNum(4, minOnes);
    d1.drawColon(true);
    d1.writeDisplay();

    d2.clear();
    if (field != PRESET_EDIT_BONUS_TENS || blinkVisible) d2.writeDigitNum(3, bonusTens);
    if (field != PRESET_EDIT_BONUS_ONES || blinkVisible) d2.writeDigitNum(4, bonusOnes);
    d2.drawColon(false);
    d2.writeDisplay();
}

// Below this, total game time is shown as MM:SS; at or above it, H:MM.
// Deliberately a lower cutover than HOUR_FORMAT_THRESHOLD_MS above: total
// game time is a running sum across the whole game, not a single player's
// remaining clock, so it's worth switching to H:MM as soon as it passes
// the one-hour mark rather than waiting for 100 minutes.
#define GAME_STATS_HOUR_THRESHOLD_MS  (60UL * 60UL * 1000UL)   // 1:00:00

// Writes a move number right-justified across all four digit positions
// (0, 1, 3, 4 — position 2 is the colon), blanking unused leading digits
// rather than showing leading zeros, and always drawing the units digit
// with its decimal point lit (e.g. "40.", or just ".5" -> "5." for
// single-digit counts) to read like standard chess move notation.
static void writeMoveCount(Adafruit_7segment &d, unsigned long count) {
    if (count > 9999UL)
        count = 9999UL;   // clamp defensively; display only has 4 digits

    uint8_t thousands = (count / 1000UL) % 10;
    uint8_t hundreds  = (count / 100UL)  % 10;
    uint8_t tens      = (count / 10UL)   % 10;
    uint8_t ones      = count % 10UL;

    d.clear();
    bool started = false;
    if (thousands > 0)            { d.writeDigitNum(0, thousands); started = true; }
    if (started || hundreds > 0)  { d.writeDigitNum(1, hundreds);  started = true; }
    if (started || tens > 0)      { d.writeDigitNum(3, tens); }
    d.writeDigitNum(4, ones, true);   // units digit, decimal point always on
    d.drawColon(false);
    d.writeDisplay();
}

// 
void showGameStats(unsigned long totalPlayTimeMs, unsigned long moveCount,
                   Adafruit_7segment &d1, Adafruit_7segment &d2) {
    if (totalPlayTimeMs > MAX_DISPLAYABLE_MS)
        totalPlayTimeMs = MAX_DISPLAYABLE_MS;

    if (totalPlayTimeMs >= GAME_STATS_HOUR_THRESHOLD_MS) {
        writeHMM(d1, totalPlayTimeMs, true);
    } else {
        writeMMSS(d1, totalPlayTimeMs);
    }

    writeMoveCount(d2, moveCount);
}

