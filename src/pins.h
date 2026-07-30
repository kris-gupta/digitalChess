#ifndef PINS_H
#define PINS_H

#include "driver/gpio.h"

// Sensor matrix wiring:
//   SER/SRCLK/RCLK are broadcast to all 8 row 74HC595s in parallel (NOT
//   daisy-chained chip-to-chip): shifting an 8-bit "walking one" pattern
//   loads the same column-select value into every row's register at once.
//   Each row's own /OE (active-low output enable) then acts as that row's
//   chip select — only the enabled row's Q outputs actually drive their
//   sensors' VCC, so exactly one sensor is ever powered at a time (one
//   column bit set x one row enabled).
//
// VERIFY THESE AGAINST YOUR ACTUAL WIRING before flashing real hardware —
// they are reasonable ESP32 GPIO choices (avoiding strapping pins 0/2/12/15,
// flash pins 6-11, UART0 pins 1/3, and GPIO16/17 which carry PSRAM on
// WROVER-variant modules) but are otherwise unverified guesses.
#define PIN_SER GPIO_NUM_25
#define PIN_SRCLK GPIO_NUM_26
#define PIN_RCLK GPIO_NUM_27

// OE_PINS[r] is the row-select line for rank (r+1), i.e. OE_PINS[0] = rank 1.
static const gpio_num_t OE_PINS[8] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_13, GPIO_NUM_14,
    GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_33,
};

// Common line all 64 hall sensor outputs share. Must be a pin with internal
// pull-up support — GPIO34-39 are input-only with NO pull resistors on the
// original ESP32 and will not work here.
#define PIN_SENSOR GPIO_NUM_32

// Shift-register column bit i (0=QA..7=QH) is assumed to power file (a+i).
// Flip the bit-order in shift_out_column() if QA is wired to file h instead.

// Logic level the sensor common line reads when a piece (magnet) is
// present. Many hall-effect ICs (e.g. A3144/SS49) are active-low
// open-drain — flip to 1 if your sensors pull the line high instead.
#define SENSOR_ACTIVE_LEVEL 0

#endif
