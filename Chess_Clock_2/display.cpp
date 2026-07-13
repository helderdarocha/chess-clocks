#include <Arduino.h>
#include "display.h"

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
#define MAX_DISPLAYABLE_MS        (9UL * 3600UL * 1000UL + 59UL * 60UL * 1000UL)  // 9:59

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

void updateDisplay(Adafruit_7segment &d, unsigned long ms) {
    if (ms > MAX_DISPLAYABLE_MS)
        ms = MAX_DISPLAYABLE_MS;

    if (ms >= HOUR_FORMAT_THRESHOLD_MS) {
        writeHMM(d, ms, true);
    } else {
        writeMMSS(d, ms);
    }
}

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
