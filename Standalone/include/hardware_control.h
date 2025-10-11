#ifndef HARDWARE_CONTROL_H
#define HARDWARE_CONTROL_H

#include <Arduino.h>

enum class PowerLogicalState { Unknown, On, Off };

enum class PowerAction { On, Off, Reset };

struct ActionFeedback
{
    const char* actionLabel;
    bool success;
    int ledLevel;
};

struct PowerControllerSnapshot
{
    PowerLogicalState logicalState;
    unsigned long stateSinceMs;
    unsigned long lastActionAtMs;
    const char* lastActionLabel;
};

struct LedTrackerSnapshot
{
    bool hasStable;
    bool hasPublished;
    int stableLevel;
    unsigned long lastStableChange;
};

void hardwareSetup();
void hardwareLoop();

ActionFeedback performPowerAction(PowerAction action);

PowerControllerSnapshot getPowerControllerSnapshot();
LedTrackerSnapshot getLedTrackerSnapshot();
void markLedStatusPublished();

bool scheduleControllerReboot(unsigned long delayMs = 1000UL);
bool isControllerRebootPending();
unsigned long getControllerRebootRemainingMs();
unsigned long getControllerRebootDelayMs();

const char* powerStateToText(PowerLogicalState state);
const char* ledLevelToStateText(int level);
#endif  // HARDWARE_CONTROL_H
