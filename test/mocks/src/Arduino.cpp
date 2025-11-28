/**
 * @file Arduino.cpp
 * @brief Arduino mock implementation for native PlatformIO unit testing
 */

#include "Arduino.h"

#ifdef NATIVE_TEST_BUILD

// =============================================================================
// TIMING MOCK STATE
// =============================================================================

uint32_t _mock_millis = 0;
uint32_t _mock_micros = 0;

// =============================================================================
// ADC MOCK STATE
// =============================================================================

int _mock_adc_resolution = 10;
uint32_t _mock_adc_values[64] = {0};

// =============================================================================
// SERIAL MOCK INSTANCE
// =============================================================================

MockSerial Serial;

#endif // NATIVE_TEST_BUILD
