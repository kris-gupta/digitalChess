#ifndef PINS_H
#define PINS_H

#include "driver/gpio.h"

// Sensor matrix wiring:
//   SER/SRCLK/RCLK are broadcast to all 8 row 74HC595s in parallel (NOT
//   daisy-chained chip-to-chip): shifting an 8-bit "walking one" pattern
//   loads the same column-select value into every row's register at once,
//   so each row's Q outputs power exactly one of that row's 8 sensors (the
//   one for the currently selected file). Every row now has its own
//   dedicated sensor-output pin (ROW_PINS below), so all 8 rows read back
//   in parallel — a full board scan is 8 shift-register loads (one per
//   file), not 64 individual row selects.
//
// VERIFY THESE AGAINST YOUR ACTUAL WIRING before flashing real hardware —
// they are reasonable ESP32 GPIO choices (avoiding strapping pins 0/2/12/15,
// flash pins 6-11, UART0 pins 1/3, and GPIO16/17 which carry PSRAM on
// WROVER-variant modules) but are otherwise unverified guesses.
#define PIN_SER GPIO_NUM_25
#define PIN_SRCLK GPIO_NUM_26
#define PIN_RCLK GPIO_NUM_27

// ROW_PINS[r] is rank (r+1)'s dedicated sensor-output pin, i.e. ROW_PINS[0]
// = rank 1. Must be pins with internal pull-up support — GPIO34-39 are
// input-only with NO pull resistors on the original ESP32 and will not work
// here.
static const gpio_num_t ROW_PINS[8] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_13, GPIO_NUM_14,
    GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_33,
};

// Shift-register column bit i (0=QA..7=QH) is assumed to power file (a+i).
// Flip the bit-order in shift_out_column() if QA is wired to file h instead.

// Logic level the sensor common line reads when a piece (magnet) is
// present. Many hall-effect ICs (e.g. A3144/SS49) are active-low
// open-drain — flip to 1 if your sensors pull the line high instead.
#define SENSOR_ACTIVE_LEVEL 0

#endif
