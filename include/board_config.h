/**
 * @file board_config.h
 * @brief Per-board hardware configuration selected by build macro
 *
 * Exactly one board macro must be defined by the build environment:
 * - BOARD_BLUEBUZZAH_NRF52:    Adafruit Feather nRF52840 Express (4 motors)
 * - BOARD_PENTABUZZER_ESP32S3: Seeed XIAO ESP32-S3 (5 motors)
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#if defined(BOARD_BLUEBUZZAH_NRF52)
  #define MAX_ACTUATORS            4
  #define NEOPIXEL_PIN_OVERRIDE    PIN_NEOPIXEL   // Feather board macro
  #define USER_BUTTON_ENABLED      1
  #define USER_BUTTON_PIN_OVERRIDE 7              // Feather USER switch (force-SECONDARY at boot)
  #define BATTERY_SENSE_ENABLED    1
  #define BATTERY_PIN_OVERRIDE     PIN_VBAT
  #define ADC_RESOLUTION_BITS      14
  #define ADC_MAX_VALUE            16383
  #define ADC_REFERENCE_VOLTAGE    3.6f
  #define BATTERY_VOLTAGE_DIVIDER  2.0f
#elif defined(BOARD_PENTABUZZER_ESP32S3)
  #define MAX_ACTUATORS            5
  #define NEOPIXEL_PIN_OVERRIDE    4              // RGB LED data (GPIO4)
  #define SDA_PIN_OVERRIDE         5
  #define SCL_PIN_OVERRIDE         6
  #define ENABLE_PIN_OVERRIDE      1              // 3V3 peripheral rail enable
  #define POWER_SWITCH_PIN_OVERRIDE 3             // strapping pin; sampled after boot only
  #define USB_POW_DETECT_PIN_OVERRIDE 2
  #define USER_BUTTON_ENABLED      0              // no user button on this board
  #define BATTERY_SENSE_ENABLED    0              // no VBAT divider / fuel gauge on this board
#else
  #error "No board macro defined (BOARD_BLUEBUZZAH_NRF52 or BOARD_PENTABUZZER_ESP32S3)"
#endif

// I2C addresses are identical on both boards
#define TCA9548A_ADDRESS  0x70
#define DRV2605_ADDRESS   0x5A

#endif // BOARD_CONFIG_H
