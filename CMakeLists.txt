cmake_minimum_required(VERSION 3.21)
project(chess_clocks_arduino NONE)

# CLion can run these targets from the gutter or Run/Debug configurations.
# Override on configure if needed:
#   -DARDUINO_FQBN=arduino:avr:nano
#   -DARDUINO_PORT=/dev/tty.usbmodemXXXX
set(ARDUINO_CLI "arduino-cli" CACHE STRING "Path to arduino-cli executable")
set(ARDUINO_FQBN "arduino:avr:nano" CACHE STRING "Board FQBN")
set(ARDUINO_PORT "" CACHE STRING "Serial port for upload")
set(SKETCH_DIR "${CMAKE_SOURCE_DIR}/Chess_Clock_i2C" CACHE PATH "Arduino sketch directory")

add_custom_target(arduino_compile
  COMMAND ${ARDUINO_CLI} compile --fqbn ${ARDUINO_FQBN} "${SKETCH_DIR}"
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
    COMMAND ${ARDUINO_CLI} upload -p ${ARDUINO_PORT} --fqbn ${ARDUINO_FQBN} "${SKETCH_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Uploading Arduino sketch to ${ARDUINO_PORT}"
  )
endif()

add_custom_target(arduino_board_list
  COMMAND ${ARDUINO_CLI} board list
  USES_TERMINAL
  COMMENT "Listing connected Arduino boards"
)

add_custom_target(arduino_libs_update
  COMMAND ${ARDUINO_CLI} lib install "Adafruit GFX Library"
  COMMAND ${ARDUINO_CLI} lib install "Adafruit LED Backpack Library"
  USES_TERMINAL
  COMMENT "Installing required Arduino libraries"
)


