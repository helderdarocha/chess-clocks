/* ================================================================
 * battery.h — Battery monitoring
 *
 * Reads VCC via the internal 1.1V reference and compares against
 * BATT_WARN_MV (config.h) to warn the user when the battery is low.
 *
 * Created by Helder da Rocha on 11/07/26.
 * ================================================================ */

//#ifndef CHESS_CLOCKS_ARDUINO_BATTERY_H
//#define CHESS_CLOCKS_ARDUINO_BATTERY_H
//#endif //CHESS_CLOCKS_ARDUINO_BATTERY_H
#pragma once

#include <Arduino.h>
#include <Adafruit_LEDBackpack.h>

// Reads the current VCC in millivolts using the internal 1.1V reference.
long readVCC_mV();

// Call once from setup(), right after the displays are initialized.
// If voltage is already below BATT_WARN_MV, blocks briefly showing a
// "BAtt Lo--" warning (with beeps) on the given displays.
void checkBatteryAtStartup(Adafruit_7segment &d1, Adafruit_7segment &d2);

// Call periodically (e.g. once per loop) during RUNNING/PAUSED.
// Internally throttles itself to at most once every BATT_CHECK_MS
// (config.h) and beeps if voltage is below BATT_WARN_MV.
void checkBattery();
