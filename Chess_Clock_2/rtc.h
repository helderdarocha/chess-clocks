/* ================================================================
 * rtc.h — Optional DS3231 real-time clock, used only to correct the
 * drift of the Arduino's own millis() timebase (ceramic resonator).
 *
 * The DS3231's absolute date/time is never read or set — only its
 * one-second tick rate is used as a reference, so no "set the clock"
 * step is required. If no DS3231 is found on the I2C bus, every
 * function here safely no-ops and correctedMillis() falls back to
 * plain millis(), so the same compiled binary works on units with or
 * without the chip installed.
 *
 * Created by Helder da Rocha on 12/07/26.
 * ================================================================ */

//#ifndef CHESS_CLOCKS_ARDUINO_RTC_H
//#define CHESS_CLOCKS_ARDUINO_RTC_H
//#endif //CHESS_CLOCKS_ARDUINO_RTC_H
#pragma once

// Call once from setup(), after Wire.begin(). Detects whether a DS3231 is
// present on the bus.
void rtcInit();

// Call once per loop() iteration. Internally throttles its own I2C reads
// and periodically recalibrates the drift-correction rate against the
// RTC. Cheap to call every iteration; no-op if no RTC was found.
void rtcUpdate();

// Call right after waking from sleep. millis() is frozen during
// SLEEP_MODE_PWR_DOWN but the RTC keeps ticking independently, so the
// in-progress calibration window is discarded and restarted from the
// post-wake reference instead of seeing a false drift reading. The
// existing correction rate keeps applying in the meantime. No-op if no
// RTC was found.
void rtcNotifyWake();

// Drop-in replacement for millis() wherever drift-corrected timing
// matters (currently: turn-time countdown in game.cpp). Falls back to
// plain millis() if no RTC was detected.
unsigned long correctedMillis();

// True if a DS3231 was detected at rtcInit(). Exposed for diagnostics;
// not required for normal operation.
bool rtcIsAvailable();
