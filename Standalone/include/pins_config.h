#ifndef STANDALONE_PINS_CONFIG_H
#define STANDALONE_PINS_CONFIG_H

// --- Pin assignments ---

#if defined(ARDUINO_ARCH_ESP32)
#define PIN_RESET 18
#define PIN_POWER_SHORT 21
#define PIN_POWER_LONG 19
#define PIN_POWER_LED 5
#elif defined(ARDUINO_UNOR4_WIFI)
#define PIN_RESET 12
#define PIN_POWER 11
#define PIN_POWER_LED 10
#else
#error "Pin assignments not defined for this board."
#endif

// --- Timing configuration ---

#define POWER_RESET_PRESS_MS 300          // Typical reset button press duration
#define POWER_SHORT_PRESS_MS 300          // For power on, on my motherboard this powers it off if already on too
#define POWER_LONG_PRESS_MS 6000          // Must be >1500ms to match typical long-press behavior
#define POWER_LED_STABLE_THRESHOLD_MS 100 // Match ESPHome default

#endif  // STANDALONE_PINS_CONFIG_H
