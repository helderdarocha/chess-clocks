/* ================================================================
 * config.h — Hardware configuration
 * Created by Helder da Rocha on 11/07/26.
 *
 * These values may change between physical clock builds (battery type, LED color, wiring).
 * Set MODEL below to match the physical unit — everything else in this file
 * is derived from it automatically.
 *
 * Currently we have:
 * - Chess clock CMX-1 - Not supported
 * - Chess clock CMX-2 - 2 AA batteries and blue display + Arduino Nano
 * - Chess clock CMX-3W - 1 LiPo battery and white display + Arduino Pro Mini
 * - Chess clock CMX-3Y - 1 LiPo battery and yellow display + Arduino Pro Mini
 * ================================================================ */

//#ifndef CHESS_CLOCKS_ARDUINO_CONFIG_H
//#define CHESS_CLOCKS_ARDUINO_CONFIG_H
//#endif //CHESS_CLOCKS_ARDUINO_CONFIG_H
#pragma once

#include <Arduino.h>

/* ----- CHOOSE THE CLOCK MODEL! -----
 * This is the only line you should need to change. Power source (and its
 * low-battery threshold) and display LED color/brightness are all derived
 * automatically from the model below — see the "Derived from MODEL" blocks
 * further down. Don't set POWER_SOURCE or DISPLAY_LED_COLOR directly.
 */
#define CMX_1   1   // Not supported
#define CMX_2   2   // 3x AA batteries, blue display, Arduino Nano
#define CMX_3W  3   // LiPo battery, white display, Arduino Pro Mini
#define CMX_3Y  4   // LiPo battery, yellow display, Arduino Pro Mini

// **** UNCOMMENT THE REQUIRED LINE ****
//#define MODEL CMX_1
//#define MODEL CMX_2
//#define MODEL CMX_3W
#define MODEL CMX_3Y

/* ---------------- Power source ---------------- */
#define BATT_3XAA  1   // 3x AA (Alkaline ~4.5V or NiMH ~3.6V full charge)
#define BATT_LIPO  2   // Single internal LiOn/LiPo cell, 3.7V nominal

// Derived from MODEL above — do not edit these two lines directly.
#if MODEL == CMX_2
  #define POWER_SOURCE  BATT_3XAA
#elif MODEL == CMX_3W || MODEL == CMX_3Y
  #define POWER_SOURCE  BATT_LIPO
#else
  #error "Unsupported or unset MODEL in config.h (CMX_1 is not supported; pick CMX_2, CMX_3W, or CMX_3Y)"
#endif

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
#define DISPLAY_LED_WHITE  1
#define DISPLAY_LED_BLUE   2
#define DISPLAY_LED_YELLOW 3
#define DISPLAY_LED_FULL   0

// **** DEPENDS on MODEL — derived below, do not edit directly ****
#if MODEL == CMX_2
  #define DISPLAY_LED_COLOR  DISPLAY_LED_BLUE
#elif MODEL == CMX_3W
  #define DISPLAY_LED_COLOR  DISPLAY_LED_WHITE
#elif MODEL == CMX_3Y
  #define DISPLAY_LED_COLOR  DISPLAY_LED_YELLOW
#else
  #error "Unsupported or unset MODEL in config.h (CMX_1 is not supported; pick CMX_2, CMX_3W, or CMX_3Y)"
#endif

// Brightness actually applied at runtime via setBrightness(); differs by LED color.
#if DISPLAY_LED_COLOR == DISPLAY_LED_WHITE
  #define DISPLAY_ON 15
#elif DISPLAY_LED_COLOR == DISPLAY_LED_BLUE
  #define DISPLAY_ON 15
#elif DISPLAY_LED_COLOR == DISPLAY_LED_YELLOW
  #define DISPLAY_ON 15  
#elif DISPLAY_LED_COLOR == DISPLAY_LED_FULL
  #define DISPLAY_ON 15
#else
  #error "DISPLAY_LED_COLOR not set in config.h"
#endif

// Used to turn off the displays
#define DISPLAY_OFF 0

// I2C addresses of the two 7-segment displays. Do not change (unless you know what you're doing).
#define DISPLAY_ADDRESS_1 0x70
#define DISPLAY_ADDRESS_2 0x71

/* ---------------- Sound ---------------- */
// Select what plays when a player's time runs out (see gameOverTune() in sound.cpp).
#define GAME_OVER_TUNE   1   // the full melody (default, unchanged)
#define GAME_OVER_BEEPS  2   // three long beeps, for those who find the melody annoying

// **** UNCOMMENT THE REQUIRED LINE ****
#define GAME_OVER_SOUND      GAME_OVER_TUNE    // <-- CHANGE
//#define GAME_OVER_SOUND      GAME_OVER_BEEPS   // <-- CHANGE

// Sounds can be turned off completely (for tournaments), pressing the "-" button for 3 seconds while PAUSED.
// The buzzer may still be used for low-battery warnings even if sound is off.

/* ---------------- Arduino Pins ---------------- */
// Reserved for pause LED if present
#define LED_PAUSE 3

// Player levers
#define PLY1      4
#define PLY2      5

// Reserved for player LEDs if present
#define LED_PLY1 6
#define LED_PLY2 7

// Control buttons
#define PAUSE_BTN 8
#define PLUS_BTN  12
#define MINUS_BTN 15

#define BUZZER    11


/* ---------------- Power saving ---------------- */
// Determines how long the clock can be idle (not RUNNING) before going to sleep.
#define IDLE_TIMEOUT_MS 300000UL  // 5 minutes