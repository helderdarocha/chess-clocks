#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include "rtc.h"

// How often we're willing to actually talk to the RTC over I2C.
#define RTC_POLL_INTERVAL_MS    250UL

// How long a calibration window must run before we trust its measurement
// enough to update the drift-correction rate. Longer windows average out
// the (up to ~one loop-iteration) jitter in exactly when we notice the
// RTC's seconds counter has ticked over, since we only poll it, we don't
// get an interrupt on the second edge.
#define CALIBRATION_WINDOW_SEC  120UL

// Sanity bound on the drift rate we're willing to believe. A cheap
// resonator might realistically be off by a few thousand ppm; anything
// beyond this is almost certainly a corrupted I2C read (noise, bad wire,
// brief bus glitch) rather than genuine drift, so that window's estimate
// is discarded and the previous correction rate is kept instead.
#define MAX_PLAUSIBLE_PPM       10000L

static RTC_DS3231 rtc;
static bool available = false;

static unsigned long lastPollMs = 0;

static bool calibrating = false;
static uint32_t calibStartEpoch = 0;
static unsigned long calibStartMillis = 0;

// Current best estimate of millis() drift, in parts per million.
// Positive means millis() under-counts real time (runs slow) and
// correctedMillis() should run faster than raw millis() to compensate.
static long correctionPpm = 0;

// Accumulated fractional correction, in ppm*ms units, carried between
// calls so small per-call adjustments aren't lost to integer truncation.
static long correctionRemainder = 0;

static unsigned long lastAppliedMillis = 0;
static unsigned long correctedBase = 0;

// Initializes the RTC module and detects whether a DS3231 is present on the I2C bus.
void rtcInit() {
  available = rtc.begin();
  lastAppliedMillis = millis();
  correctedBase = lastAppliedMillis;
  // Deliberately never read/set the RTC's date-time: only its tick rate
  // is used, so the chip never needs to be told the correct wall-clock
  // time for this drift correction to work.
}

// Returns true if a DS3231 was detected at rtcInit(). Exposed for diagnostics;
// not required for normal operation.
bool rtcIsAvailable() {
  return available;
}

// Internal helper to start a new calibration window. Updates the
// reference epoch and millis() values, and sets the calibrating flag.
static void restartCalibrationWindow(uint32_t epoch, unsigned long nowMs) {
  calibStartEpoch  = epoch;
  calibStartMillis = nowMs;
  calibrating      = true;
}

// Call right after waking from sleep. millis() is frozen during
// SLEEP_MODE_PWR_DOWN but the RTC keeps ticking independently, so the
// in-progress calibration window is discarded and restarted from the
// post-wake reference instead of seeing a false drift reading. The
// existing correction rate keeps applying in the meantime. No-op if no
// RTC was found.
void rtcNotifyWake() {
  if (!available)
    return;
  // Discard whatever window was in progress; correctionPpm itself (the
  // last known-good rate) keeps being applied without interruption.
  calibrating = false;
}

// Call once per loop() iteration. Internally throttles its own I2C reads
// and periodically recalibrates the drift-correction rate against the
// RTC. Cheap to call every iteration; no-op if no RTC was found.
void rtcUpdate() {
  if (!available)
    return;

  unsigned long now = millis();
  if (now - lastPollMs < RTC_POLL_INTERVAL_MS)
    return;
  lastPollMs = now;

  uint32_t epoch = rtc.now().unixtime();   // the only I2C read this module does

  if (!calibrating) {
    restartCalibrationWindow(epoch, now);
    return;
  }

  uint32_t elapsedSec = epoch - calibStartEpoch;
  if (elapsedSec < CALIBRATION_WINDOW_SEC)
    return;

  unsigned long elapsedRawMs = now - calibStartMillis;
  unsigned long expectedMs   = elapsedSec * 1000UL;
  long diffMs = (long)expectedMs - (long)elapsedRawMs;

  long candidatePpm = (long)(((int64_t)diffMs * 1000000LL) / (int64_t)elapsedRawMs);

  // Only accept the new estimate if it's a plausible drift rate; otherwise
  // keep the previous correctionPpm and just restart the window. A single
  // bad reading (e.g. a glitched I2C transaction) then costs one window's
  // worth of staleness rather than corrupting the running correction.
  if (candidatePpm >= -MAX_PLAUSIBLE_PPM && candidatePpm <= MAX_PLAUSIBLE_PPM) {
    correctionPpm = candidatePpm;
  }

  restartCalibrationWindow(epoch, now);
}

// Returns a drift-corrected millis() value, using the RTC as a reference.
// If no RTC was detected at startup, falls back to plain millis().
unsigned long correctedMillis() {
  if (!available)
    return millis();

  unsigned long rawNow   = millis();
  unsigned long rawDelta = rawNow - lastAppliedMillis;
  lastAppliedMillis = rawNow;

  long numerator = (long)rawDelta * correctionPpm + correctionRemainder;
  long adjust    = numerator / 1000000L;
  correctionRemainder = numerator % 1000000L;

  correctedBase += (unsigned long)((long)rawDelta + adjust);
  return correctedBase;
}