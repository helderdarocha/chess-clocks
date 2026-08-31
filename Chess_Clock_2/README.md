# Chess Clock 2 - codebase

This project is a chess clock implemented using an Arduino board. The code is written in C++ and uses the Arduino 
framework. The main functionality of the clock is to manage the timing of chess games. Time controls
are saved as presets, which the user can select before starting a game. The clock supports the most popular time 
controls, including Bullet, Blitz, Rapid, and Classic formats, including bonus time, and the user can also save a 
custom time control and bonus. It is a reliable and easy-to-use chess clock that can be used in tournaments.

Time switches between players are handled by a lever, which the players press to indicate that they have completed 
their turn. The Arduino reads the lever's state and switches the active clock, adding bonus time if applicable. 
During the game, the user can pause the clock, adjust the time for penalties (adding or subtracting minutes, and
resume the countdown. 

Other features include a low-battery warning (which appears as a message at startup and a beep), sleep mode for power 
saving, stats display (total time and number of moves), options to turn the sound on or off, and an optional game-over 
melody when a player's time runs out. The code also supports a DS3231 real-time clock module to correct the drift in
boards that don't have a precise crystal oscillator (most Arduino boards).

The clock can display time in MM:SS format up to 99:59. Larger values are shown in H:MM format. The maximum supported 
time is 9:59:59.

This code is used by three models of chess clocks, which differ in the power supply and display type. The model must be
selected in the `config.h` file before compiling the code. 

The code is designed to be modular, with separate files for battery management, display rendering, sounds, 
power management, game state, and real-time clock (RTC) support.

Below is a brief description of the main files in the codebase:

- **Chess_Clock_2.ino**: This is the main sketch file. It includes the other files and contains the standard Arduino 
   `setup()` and `loop()` functions.
- **config.h**: This file contains the per-unit hardware configuration. It defines the battery type, display type,
    and pin assignments for the buttons and lever. The user must modify this file to match the hardware configuration
    of their specific chess clock model before compiling the code.
- **game.h** / **game.cpp**: This is the main code of the chess clock. It handles the game state machine and button
    handling. The code manages the different states of the clock (selecting game duration, paused, running, finished,
    time adjustment, etc.), button presses and lever movement. The code also defines the presets for the different time
    controls and bonus time. Its main function (`clockUpdate()`) is called from the main loop and updates the clock state
    based on the current state and user input.
- **display.h** / **display.cpp**: These files handle the rendering of the 7-segment displays. The functions in these
    files are responsible for displaying the time, game duration, and status messages on the displays (except for
    the "bAtt Lo--" message, which is handled by the battery code), and turning on and off the displays as needed.
- **battery.h** / **battery.cpp**: These files handle battery voltage reading and low-battery warnings. The code reads the
    battery voltage and displays a warning message if the voltage drops below a certain threshold. The threshold depends
    on the battery type and is defined in the `config.h` file. The threshold for AA batteries was set based on Alkaline
    batteries, and may need to be adjusted for NiMH batteries, for example. The code also includes a low-battery warning
    beep and shows "bAtt Lo--" at startup on the display when the battery is low.
- **power.h** / **power.cpp**: These files handle the sleep mode for power saving. The function puts the Arduino into
    sleep mode after a certain period of inactivity (no button presses or lever movement and not running a game). The
    clock leaves sleep mode when a button is pressed or the lever is moved.
- **rtc.h** / **rtc.cpp**: These files handle the optional DS3231 real-time clock module. The code reads the time from
    the DS3231 and uses it to correct the drift in the Arduino's `millis()` function. If a DS3231 is not present, the code
    will still work, but the time may drift if the board used does not have a precise crystal oscillator.
- **sound.h** / **sound.cpp**: These files handle the buzzer tones. The code generates different
    tones for button presses, low-battery warnings, and a melody (or three long beeps) when a player's time runs out.
    The sound can be turned on or off by the user using the minus and plus buttons. Some sound options are set in the
    `config.h` file (use beeps or a game-over melody).
