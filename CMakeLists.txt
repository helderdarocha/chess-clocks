cmake_minimum_required(VERSION 3.21)
project(chess_clocks_arduino C CXX)

# CLion can run these targets from the gutter or Run/Debug configurations.
# Override on configure if needed:
#   -DARDUINO_CLI=/opt/homebrew/bin/arduino-cli
#   -DARDUINO_FQBN=arduino:avr:nano
#   -DARDUINO_PORT=/dev/tty.usbmodemXXXX
#   -DSKETCH_DIR=/absolute/path/to/sketch
set(ARDUINO_CLI "arduino-cli" CACHE STRING "Path (or command name) for arduino-cli")
set(ARDUINO_FQBN "arduino:avr:nano" CACHE STRING "Board FQBN")
set(ARDUINO_PORT "" CACHE STRING "Serial port for upload")

# ====================
# Choose version commenting out the other:
# set(SKETCH_DIR "${CMAKE_SOURCE_DIR}/Chess_Clock_i2C_v2" CACHE PATH "Arduino sketch directory")
set(SKETCH_DIR "${CMAKE_SOURCE_DIR}/Chess_Clock_2" CACHE PATH "Arduino sketch directory")
# set(SKETCH_DIR "${CMAKE_SOURCE_DIR}/Chess_Clock_i2C" CACHE PATH "Arduino sketch directory")
# ====================

set(ARDUINO_REQUIRED_CORE "arduino:avr" CACHE STRING "Arduino core required by this project")
set(ARDUINO_REQUIRED_LIBS "Adafruit GFX Library;Adafruit LED Backpack Library" CACHE STRING "Required Arduino libraries")

find_program(ARDUINO_CLI_EXECUTABLE
  NAMES ${ARDUINO_CLI} arduino-cli
  HINTS
    "$ENV{ARDUINO_CLI}"
    "/opt/homebrew/bin"
    "/usr/local/bin"
)

if(NOT ARDUINO_CLI_EXECUTABLE)
  message(FATAL_ERROR
    "arduino-cli not found. Set -DARDUINO_CLI=/absolute/path/to/arduino-cli (for example /opt/homebrew/bin/arduino-cli)."
  )
endif()

if(NOT EXISTS "${SKETCH_DIR}")
  message(FATAL_ERROR "SKETCH_DIR does not exist: ${SKETCH_DIR}")
endif()

message(STATUS "Using arduino-cli: ${ARDUINO_CLI_EXECUTABLE}")
message(STATUS "Using sketch dir : ${SKETCH_DIR}")

# Provide Arduino include paths to the IDE code model so symbols like
# Arduino.h/PROGMEM/tone() resolve in .cpp modules.
execute_process(
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" compile --fqbn "${ARDUINO_FQBN}" --show-properties "${SKETCH_DIR}"
  OUTPUT_VARIABLE ARDUINO_SHOW_PROPERTIES
  ERROR_VARIABLE ARDUINO_SHOW_PROPERTIES_ERR
  RESULT_VARIABLE ARDUINO_SHOW_PROPERTIES_RC
)

set(ARDUINO_INCLUDE_DIRS "${SKETCH_DIR}")

# Extract the platform path from the show-properties output
if(ARDUINO_SHOW_PROPERTIES_RC EQUAL 0)
  string(REGEX MATCH "runtime\\.platform\\.path=([^\n\r]+)" _ "${ARDUINO_SHOW_PROPERTIES}")
  set(ARDUINO_PLATFORM_PATH "${CMAKE_MATCH_1}")

  # Add standard Arduino core and variants include directories
  if(ARDUINO_PLATFORM_PATH)
    list(APPEND ARDUINO_INCLUDE_DIRS "${ARDUINO_PLATFORM_PATH}/cores/arduino")
    list(APPEND ARDUINO_INCLUDE_DIRS "${ARDUINO_PLATFORM_PATH}/variants/eightanaloginputs")
    message(STATUS "Arduino platform path: ${ARDUINO_PLATFORM_PATH}")
  endif()

  # Try to extract any additional -I flags
  string(REGEX MATCHALL "-I[^ \n\r\t]+" ARDUINO_INCLUDE_FLAGS "${ARDUINO_SHOW_PROPERTIES}")
  foreach(flag IN LISTS ARDUINO_INCLUDE_FLAGS)
    string(SUBSTRING "${flag}" 2 -1 inc)
    list(APPEND ARDUINO_INCLUDE_DIRS "${inc}")
  endforeach()
  list(REMOVE_DUPLICATES ARDUINO_INCLUDE_DIRS)
else()
  message(WARNING "Could not query Arduino include paths for IDE indexing: ${ARDUINO_SHOW_PROPERTIES_ERR}")
endif()

message(STATUS "Arduino include directories: ${ARDUINO_INCLUDE_DIRS}")

file(GLOB ARDUINO_CPP_SOURCES "${SKETCH_DIR}/*.c" "${SKETCH_DIR}/*.cpp")
file(GLOB ARDUINO_IDE_SOURCES "${SKETCH_DIR}/*.ino" "${SKETCH_DIR}/*.h" "${SKETCH_DIR}/*.cpp" "${SKETCH_DIR}/*.c")

if(ARDUINO_CPP_SOURCES)
  add_library(arduino_ide_dummy EXCLUDE_FROM_ALL ${ARDUINO_CPP_SOURCES}
          Chess_Clock_2/Chess_Clock_2.ino
          Chess_Clock_2/config.h
          Chess_Clock_2/battery.h
          Chess_Clock_2/battery.cpp
          Chess_Clock_2/sound.h
          Chess_Clock_2/sound.cpp
          Chess_Clock_2/display.h
          Chess_Clock_2/display.cpp
          Chess_Clock_2/power.h
          Chess_Clock_2/power.cpp
          Chess_Clock_2/game.h
          Chess_Clock_2/game.cpp
          Chess_Clock_2/rtc.cpp
          Chess_Clock_2/rtc.h)
  target_include_directories(arduino_ide_dummy SYSTEM PRIVATE ${ARDUINO_INCLUDE_DIRS})
endif()

add_custom_target(arduino_ide_index SOURCES ${ARDUINO_IDE_SOURCES})

add_custom_target(arduino_deps_update_index
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" core update-index
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" lib update-index
  USES_TERMINAL
  COMMENT "Updating Arduino core/library indexes"
)

add_custom_target(arduino_deps_install
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" core install "${ARDUINO_REQUIRED_CORE}"
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" lib install "Adafruit GFX Library"
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" lib install "Adafruit LED Backpack Library"
  USES_TERMINAL
  COMMENT "Installing required Arduino core and libraries"
)

add_custom_target(arduino_deps_check
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" core list
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" lib list
  USES_TERMINAL
  COMMENT "Checking installed Arduino cores and libraries"
)

add_custom_target(arduino_compile
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" compile --fqbn ${ARDUINO_FQBN} "${SKETCH_DIR}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Compiling Arduino sketch for ${ARDUINO_FQBN}"
)

if(ARDUINO_PORT STREQUAL "")
  add_custom_target(arduino_upload
    COMMAND ${CMAKE_COMMAND} -E echo "Set -DARDUINO_PORT=/dev/tty.usbmodemXXXX to enable upload target"
    USES_TERMINAL
    COMMENT "Upload disabled: ARDUINO_PORT is not configured"
  )
else()
  add_custom_target(arduino_upload
    COMMAND "${ARDUINO_CLI_EXECUTABLE}" upload -p ${ARDUINO_PORT} --fqbn ${ARDUINO_FQBN} "${SKETCH_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Uploading Arduino sketch to ${ARDUINO_PORT}"
  )
endif()

add_custom_target(arduino_board_list
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" board list
  USES_TERMINAL
  COMMENT "Listing connected Arduino boards"
)

add_custom_target(arduino_libs_update
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" core update-index
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" lib update-index
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" core install "${ARDUINO_REQUIRED_CORE}"
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" lib install "Adafruit GFX Library"
  COMMAND "${ARDUINO_CLI_EXECUTABLE}" lib install "Adafruit LED Backpack Library"
  USES_TERMINAL
  COMMENT "Updating indexes and installing required Arduino dependencies"
)


