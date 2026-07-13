/* ================================================================
 * config.h — Hardware configuration
 * Created by Helder da Rocha on 11/07/26.
 *
 * These values may change between physical clock builds (battery type, LED color, wiring).
 * Edit the two "<-- CHANGE" selectors below to match the correct hardware.
 *
 * Currently we have:
 * - Chess clock 2 - 2 AA batteries and blue display
 * - Chess clock 3 - 1 LiPo battery and white display
 * ================================================================ */

//#ifndef CHESS_CLOCKS_ARDUINO_CONFIG_H
//#define CHESS_CLOCKS_ARDUINO_CONFIG_H
//#endif //CHESS_CLOCKS_ARDUINO_CONFIG_H
#pragma once

#include <Arduino.h>

/* ---------------- Power source ---------------- */
// Select the power source used by THIS physical unit.
#define BATT_3XAA  1   // 3x AA (Alkaline ~4.5V or NiMH ~3.6V full charge)
#define BATT_LIPO  2   // Single internal LiOn/LiPo cell, 3.7V nominal

// **** UNCOMMENT THE REQUIRED LINE ****
//#define POWER_SOURCE        BATT_LIPO   // <-- CHANGE
#define POWER_SOURCE        BATT_3XAA   // <-- CHANGE

// Low battery threshold, considering ~1.1V AREF and protection circuit drop
#if POWER_SOURCE == BATT_3XAA
  #define BATT_WARN_MV  2950UL   // 3 AA batteries + Schottky diode (~3.0V on VCC pin at minimum)
#elif POWER_SOURCE == BATT_LIPO
  #define BATT_WARN_MV  3050UL   // Internal Molicell M35A 3.7V LiOn (~2.7V on VCC pin at minimum)
#else
  #error "POWER_SOURCE not set in config.h"
#endif

// Periodic battery check interval (ms). Checked at startup and then at most once every BATT_CHECK_MS.
#define BATT_CHECK_MS  60000UL   // 60 seconds


/* ---------------- Display ---------------- */
// Select the LED color used by THIS unit's displays.
#define DISPLAY_LED_WHITE 1
#define DISPLAY_LED_BLUE  2

// **** UNCOMMENT THE REQUIRED LINE ****
//#define DISPLAY_LED_COLOR      DISPLAY_LED_WHITE   // <-- CHANGE
#define DISPLAY_LED_COLOR      DISPLAY_LED_BLUE   // <-- CHANGE

// Brightness actually applied at runtime via setBrightness(); differs by LED color.
#if DISPLAY_LED_COLOR == DISPLAY_LED_WHITE
  #define DISPLAY_ON 7
#elif DISPLAY_LED_COLOR == DISPLAY_LED_BLUE
  #define DISPLAY_ON 10
#else
  #error "DISPLAY_LED_COLOR not set in config.h"
#endif

// Used to turn off the displays
#define DISPLAY_OFF 0

// I2C addresses of the two 7-segment displays. Do not change (unless you know what you're doing).
#define DISPLAY_ADDRESS_1 0x70
#define DISPLAY_ADDRESS_2 0x71


/* ---------------- Arduino Pins ---------------- */
// Player levers
#define PLY1      4
#define PLY2      5

// Control buttons
#define PAUSE_BTN 8
#define PLUS_BTN  12
#define MINUS_BTN 15

#define BUZZER    11


/* ---------------- Power saving ---------------- */
// Determines how long the clock can be idle (not RUNNING) before going to sleep.
#define IDLE_TIMEOUT_MS 300000UL  // 5 minutes