#include "pins_config.h"
#include "hardware_control.h"

#include <Arduino.h>
#include <cstring>

#if defined(ARDUINO_ARCH_AVR)
#include <avr/wdt.h>
#endif

#if defined(ARDUINO_ARCH_ESP8266)
#include <Esp.h>
#endif

#if !defined(OUTPUT_OPEN_DRAIN) && defined(OUTPUT_OPENDRAIN)
#define OUTPUT_OPEN_DRAIN OUTPUT_OPENDRAIN
#endif

#if defined(OUTPUT_OPEN_DRAIN)
constexpr bool CONTROL_PINS_SUPPORT_OPEN_DRAIN = true;
#else
constexpr bool CONTROL_PINS_SUPPORT_OPEN_DRAIN = false;
#endif

#ifdef PIN_POWER
#define POWER_OUTPUT_SINGLE_PIN 1
#else
#define POWER_OUTPUT_SINGLE_PIN 0
#endif

#if POWER_OUTPUT_SINGLE_PIN
#define PIN_OUTPUT_POWER_SHORT PIN_POWER
#define PIN_OUTPUT_POWER_LONG PIN_POWER
#else
#define PIN_OUTPUT_POWER_SHORT PIN_POWER_SHORT
#define PIN_OUTPUT_POWER_LONG PIN_POWER_LONG
#endif

struct PowerLedTracker
{
    int stableLevel;
    int candidateLevel;
    unsigned long candidateStart;
    unsigned long lastStableChange;
    bool hasStable;
    bool hasPublished;
};

struct PowerController
{
    PowerLogicalState logicalState;
    unsigned long stateSinceMs;
    unsigned long lastActionAtMs;
    const char* lastActionLabel;
};

static PowerLedTracker powerLedTracker;
static PowerController powerController;
static bool controllerRebootPending = false;
static unsigned long controllerRebootAtMs = 0;
static unsigned long controllerRebootDelayMs = 0;

static void performControllerReboot();

#if CONTROL_PINS_SUPPORT_OPEN_DRAIN
static inline void configureControlPin(uint8_t pin)
{
    digitalWrite(pin, HIGH);
    pinMode(pin, OUTPUT_OPEN_DRAIN);
}

static inline void setControlPinIdle(uint8_t pin)
{
    digitalWrite(pin, HIGH);
}

static inline void driveControlPinActive(uint8_t pin)
{
    digitalWrite(pin, LOW);
}
#else
static inline void configureControlPin(uint8_t pin)
{
    pinMode(pin, INPUT);
}

static inline void setControlPinIdle(uint8_t pin)
{
    pinMode(pin, INPUT);
}

static inline void driveControlPinActive(uint8_t pin)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}
#endif

static void updatePowerState(PowerLogicalState newState, const char* actionLabel)
{
    unsigned long now = millis();
    if (actionLabel != nullptr)
    {
        powerController.lastActionLabel = actionLabel;
        powerController.lastActionAtMs = now;
    }
    if (powerController.logicalState != newState)
    {
        powerController.logicalState = newState;
        powerController.stateSinceMs = now;
    }
}

const char* powerStateToText(PowerLogicalState state)
{
    switch (state)
    {
    case PowerLogicalState::On:
        return "on";
    case PowerLogicalState::Off:
        return "off";
    default:
        return "unknown";
    }
}

const char* ledLevelToStateText(int level)
{
    return (level == HIGH) ? "on" : "off";
}

const char* actionLabelFromEnum(PowerAction action)
{
    switch (action)
    {
    case PowerAction::On:
        return "Power On";
    case PowerAction::Off:
        return "Power Off";
    case PowerAction::Reset:
        return "Power Reset";
    default:
        return "Unknown Action";
    }
}

static void ensurePowerOutputsIdle()
{
    setControlPinIdle(PIN_OUTPUT_POWER_SHORT);
#if !POWER_OUTPUT_SINGLE_PIN
    setControlPinIdle(PIN_OUTPUT_POWER_LONG);
#endif
    setControlPinIdle(PIN_RESET);
}

static void pulseOutput(uint8_t pin, unsigned long durationMs)
{
    ensurePowerOutputsIdle();
    driveControlPinActive(pin);
    delay(durationMs);
    setControlPinIdle(pin);
}

static void initialisePowerController()
{
    if (powerLedTracker.hasStable)
    {
        powerController.logicalState =
            (powerLedTracker.stableLevel == HIGH) ? PowerLogicalState::On : PowerLogicalState::Off;
        unsigned long now = millis();
        powerController.stateSinceMs = now;
    }
    else
    {
        powerController.logicalState = PowerLogicalState::Unknown;
        powerController.stateSinceMs = 0;
    }
    powerController.lastActionAtMs = 0;
    powerController.lastActionLabel = nullptr;
}

static void updatePowerLedTracker()
{
    int reading = digitalRead(PIN_POWER_LED);
    unsigned long now = millis();

    if (reading != powerLedTracker.candidateLevel)
    {
        powerLedTracker.candidateLevel = reading;
        powerLedTracker.candidateStart = now;
    }

    if (!powerLedTracker.hasStable)
    {
        if (now - powerLedTracker.candidateStart >= POWER_LED_STABLE_THRESHOLD_MS)
        {
            powerLedTracker.hasStable = true;
            powerLedTracker.stableLevel = powerLedTracker.candidateLevel;
            powerLedTracker.lastStableChange = now;
        }
        return;
    }

    if (reading == powerLedTracker.stableLevel)
    {
        powerLedTracker.candidateLevel = reading;
        powerLedTracker.candidateStart = now;
        return;
    }

    if (reading == powerLedTracker.candidateLevel)
    {
        if (now - powerLedTracker.candidateStart >= POWER_LED_STABLE_THRESHOLD_MS)
        {
            powerLedTracker.stableLevel = reading;
            powerLedTracker.lastStableChange = now;
        }
    }
    else
    {
        powerLedTracker.candidateLevel = reading;
        powerLedTracker.candidateStart = now;
    }
}

static ActionFeedback executeAction(PowerAction action)
{
    ActionFeedback feedback{};
    feedback.actionLabel = actionLabelFromEnum(action);

    switch (action)
    {
    case PowerAction::On:
        pulseOutput(PIN_OUTPUT_POWER_SHORT, POWER_SHORT_PRESS_MS);
        updatePowerState(PowerLogicalState::On, feedback.actionLabel);
        break;
    case PowerAction::Off:
        pulseOutput(PIN_OUTPUT_POWER_LONG, POWER_LONG_PRESS_MS);
        updatePowerState(PowerLogicalState::Off, feedback.actionLabel);
        break;
    case PowerAction::Reset:
        pulseOutput(PIN_RESET, POWER_RESET_PRESS_MS);
        updatePowerState(PowerLogicalState::On, feedback.actionLabel);
        break;
    }

    feedback.success = true;
    feedback.ledLevel = digitalRead(PIN_POWER_LED);

    Serial.print("Action ");
    Serial.print(feedback.actionLabel);
    Serial.print(": executed; LED pin reads ");
    Serial.println(feedback.ledLevel == HIGH ? "HIGH" : "LOW");

    return feedback;
}

static void performControllerReboot()
{
    ensurePowerOutputsIdle();

    Serial.println("Controller rebooting now...");
    Serial.flush();
    delay(50);

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    ESP.restart();
#elif defined(ARDUINO_ARCH_AVR)
    wdt_enable(WDTO_15MS);
    for (;;) {}
#elif defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_STM32) || defined(ARDUINO_UNOR4_WIFI) || defined(ARDUINO_UNOR4_MINIMA) || defined(ARDUINO_ARCH_RENESAS) || defined(ARDUINO_ARCH_MBED)
    NVIC_SystemReset();
#else
    NVIC_SystemReset();
#endif

    while (true)
    {
        delay(10);
    }
}

void hardwareSetup()
{
#if POWER_OUTPUT_SINGLE_PIN
    configureControlPin(PIN_OUTPUT_POWER_SHORT);
#else
    configureControlPin(PIN_OUTPUT_POWER_SHORT);
    configureControlPin(PIN_OUTPUT_POWER_LONG);
#endif
    configureControlPin(PIN_RESET);
    ensurePowerOutputsIdle();

#if defined(ARDUINO_ARCH_ESP32)
    pinMode(PIN_POWER_LED, INPUT_PULLDOWN);
#else
    pinMode(PIN_POWER_LED, INPUT);
#endif

    powerLedTracker.candidateLevel = digitalRead(PIN_POWER_LED);
    powerLedTracker.candidateStart = millis();
    powerLedTracker.stableLevel = powerLedTracker.candidateLevel;
    powerLedTracker.lastStableChange = powerLedTracker.candidateStart;
    powerLedTracker.hasStable = true;
    powerLedTracker.hasPublished = false;
    updatePowerLedTracker();
    initialisePowerController();
    Serial.println("Power control outputs ready");
}

void hardwareLoop()
{
    updatePowerLedTracker();

    if (controllerRebootPending)
    {
        unsigned long now = millis();
        if (now >= controllerRebootAtMs)
        {
            controllerRebootPending = false;
            performControllerReboot();
        }
    }
}

ActionFeedback performPowerAction(PowerAction action)
{
    return executeAction(action);
}

bool scheduleControllerReboot(unsigned long delayMs)
{
    if (controllerRebootPending)
    {
        return false;
    }

    unsigned long now = millis();
    controllerRebootPending = true;
    controllerRebootDelayMs = delayMs;
    controllerRebootAtMs = now + delayMs;

    Serial.print("Controller reboot scheduled in ");
    Serial.print(delayMs);
    Serial.println(" ms");

    return true;
}

bool isControllerRebootPending()
{
    return controllerRebootPending;
}

unsigned long getControllerRebootRemainingMs()
{
    if (!controllerRebootPending)
    {
        return 0;
    }

    unsigned long now = millis();
    if (now >= controllerRebootAtMs)
    {
        return 0;
    }
    return controllerRebootAtMs - now;
}

unsigned long getControllerRebootDelayMs()
{
    return controllerRebootDelayMs;
}

PowerControllerSnapshot getPowerControllerSnapshot()
{
    PowerControllerSnapshot snapshot{};
    snapshot.logicalState = powerController.logicalState;
    snapshot.stateSinceMs = powerController.stateSinceMs;
    snapshot.lastActionAtMs = powerController.lastActionAtMs;
    snapshot.lastActionLabel = powerController.lastActionLabel;
    return snapshot;
}

LedTrackerSnapshot getLedTrackerSnapshot()
{
    LedTrackerSnapshot snapshot{};
    snapshot.hasStable = powerLedTracker.hasStable;
    snapshot.hasPublished = powerLedTracker.hasPublished;
    snapshot.stableLevel = powerLedTracker.stableLevel;
    snapshot.lastStableChange = powerLedTracker.lastStableChange;
    return snapshot;
}

void markLedStatusPublished()
{
    powerLedTracker.hasPublished = true;
}
