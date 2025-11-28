/**
 * @file Arduino.h
 * @brief Arduino core mocks for native PlatformIO unit testing
 * @note This mock is only compiled when NATIVE_TEST_BUILD is defined
 */

#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#ifdef NATIVE_TEST_BUILD

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>

// =============================================================================
// ARDUINO TYPE DEFINITIONS
// =============================================================================

typedef uint8_t byte;
typedef bool boolean;

// =============================================================================
// PIN MODES AND VALUES
// =============================================================================

#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LOW 0
#define HIGH 1

// =============================================================================
// MOCK PIN DEFINITIONS (nRF52840 Feather)
// =============================================================================

#ifndef PIN_NEOPIXEL
#define PIN_NEOPIXEL 8
#endif

#ifndef PIN_VBAT
#define PIN_VBAT 31
#endif

#ifndef PIN_WIRE_SDA
#define PIN_WIRE_SDA 25
#endif

#ifndef PIN_WIRE_SCL
#define PIN_WIRE_SCL 26
#endif

// =============================================================================
// TIMING FUNCTIONS
// =============================================================================

// Mock timing state - can be manipulated by tests
extern uint32_t _mock_millis;
extern uint32_t _mock_micros;

/**
 * @brief Get mock milliseconds since start
 */
inline uint32_t millis() { return _mock_millis; }

/**
 * @brief Get mock microseconds since start
 */
inline uint32_t micros() { return _mock_micros; }

/**
 * @brief Mock delay (advances mock time)
 */
inline void delay(uint32_t ms) {
    _mock_millis += ms;
    _mock_micros += ms * 1000;
}

/**
 * @brief Mock microsecond delay (advances mock time)
 */
inline void delayMicroseconds(uint32_t us) {
    _mock_micros += us;
    _mock_millis = _mock_micros / 1000;
}

// =============================================================================
// MOCK TIMING CONTROL FOR TESTS
// =============================================================================

/**
 * @brief Reset mock time to zero
 */
inline void mockResetTime() {
    _mock_millis = 0;
    _mock_micros = 0;
}

/**
 * @brief Advance mock time by milliseconds
 */
inline void mockAdvanceMillis(uint32_t ms) {
    _mock_millis += ms;
    _mock_micros += ms * 1000;
}

/**
 * @brief Advance mock time by microseconds
 */
inline void mockAdvanceMicros(uint32_t us) {
    _mock_micros += us;
    _mock_millis = _mock_micros / 1000;
}

/**
 * @brief Set mock time directly
 */
inline void mockSetMillis(uint32_t ms) {
    _mock_millis = ms;
    _mock_micros = ms * 1000;
}

// =============================================================================
// ADC FUNCTIONS
// =============================================================================

extern int _mock_adc_resolution;
extern uint32_t _mock_adc_values[64];

/**
 * @brief Set ADC resolution (mock)
 */
inline void analogReadResolution(int bits) {
    _mock_adc_resolution = bits;
}

/**
 * @brief Read ADC value (mock - returns preset value)
 */
inline uint32_t analogRead(uint8_t pin) {
    if (pin < 64) {
        return _mock_adc_values[pin];
    }
    return 0;
}

/**
 * @brief Set mock ADC value for a pin (for test setup)
 */
inline void mockSetADCValue(uint8_t pin, uint32_t value) {
    if (pin < 64) {
        _mock_adc_values[pin] = value;
    }
}

// =============================================================================
// RANDOM FUNCTIONS
// =============================================================================

/**
 * @brief Seed random number generator
 */
inline void randomSeed(unsigned long seed) {
    srand(seed);
}

/**
 * @brief Generate random number [0, max)
 */
inline long random(long max) {
    if (max <= 0) return 0;
    return rand() % max;
}

/**
 * @brief Generate random number [min, max)
 */
inline long random(long min, long max) {
    if (max <= min) return min;
    return min + (rand() % (max - min));
}

// =============================================================================
// PIN FUNCTIONS (NO-OP STUBS)
// =============================================================================

inline void pinMode(uint8_t pin, uint8_t mode) {
    (void)pin;
    (void)mode;
}

inline void digitalWrite(uint8_t pin, uint8_t value) {
    (void)pin;
    (void)value;
}

inline int digitalRead(uint8_t pin) {
    (void)pin;
    return LOW;
}

// =============================================================================
// FLASH STRING HELPER
// =============================================================================

#define F(str) str
#define PROGMEM

// =============================================================================
// SERIAL MOCK
// =============================================================================

/**
 * @brief Mock Serial class for testing
 *
 * All methods are no-ops to prevent output during tests.
 * Can be extended to capture output if needed.
 */
class MockSerial {
public:
    void begin(unsigned long baud) { (void)baud; }
    void end() {}

    // Print functions (no-op)
    size_t print(const char* s) { (void)s; return 0; }
    size_t print(char c) { (void)c; return 0; }
    size_t print(int n, int base = 10) { (void)n; (void)base; return 0; }
    size_t print(unsigned int n, int base = 10) { (void)n; (void)base; return 0; }
    size_t print(long n, int base = 10) { (void)n; (void)base; return 0; }
    size_t print(unsigned long n, int base = 10) { (void)n; (void)base; return 0; }
    size_t print(double n, int digits = 2) { (void)n; (void)digits; return 0; }

    size_t println(const char* s = "") { (void)s; return 0; }
    size_t println(char c) { (void)c; return 0; }
    size_t println(int n, int base = 10) { (void)n; (void)base; return 0; }
    size_t println(unsigned int n, int base = 10) { (void)n; (void)base; return 0; }
    size_t println(long n, int base = 10) { (void)n; (void)base; return 0; }
    size_t println(unsigned long n, int base = 10) { (void)n; (void)base; return 0; }
    size_t println(double n, int digits = 2) { (void)n; (void)digits; return 0; }

    // Printf (no-op)
    size_t printf(const char* format, ...) {
        (void)format;
        return 0;
    }

    // Status functions
    operator bool() { return true; }
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    void flush() {}
};

extern MockSerial Serial;

// =============================================================================
// STRING CLASS STUB (minimal implementation)
// =============================================================================

class String {
public:
    String() : _buffer(nullptr), _length(0) {}
    String(const char* str) {
        if (str) {
            _length = strlen(str);
            _buffer = new char[_length + 1];
            strcpy(_buffer, str);
        } else {
            _buffer = nullptr;
            _length = 0;
        }
    }
    String(const String& other) {
        if (other._buffer) {
            _length = other._length;
            _buffer = new char[_length + 1];
            strcpy(_buffer, other._buffer);
        } else {
            _buffer = nullptr;
            _length = 0;
        }
    }
    ~String() {
        delete[] _buffer;
    }

    String& operator=(const String& other) {
        if (this != &other) {
            delete[] _buffer;
            if (other._buffer) {
                _length = other._length;
                _buffer = new char[_length + 1];
                strcpy(_buffer, other._buffer);
            } else {
                _buffer = nullptr;
                _length = 0;
            }
        }
        return *this;
    }

    const char* c_str() const { return _buffer ? _buffer : ""; }
    unsigned int length() const { return _length; }
    bool isEmpty() const { return _length == 0; }

private:
    char* _buffer;
    unsigned int _length;
};

#endif // NATIVE_TEST_BUILD
#endif // MOCK_ARDUINO_H
