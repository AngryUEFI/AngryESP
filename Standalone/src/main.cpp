#include <Arduino.h>

#include "hardware_control.h"
#include "web_interface.h"

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {}
    Serial.println("\nAngryESP Standalone Control booting...");

    hardwareSetup();
    webSetup();
    Serial.println("Setup complete.");
}

void loop()
{
    hardwareLoop();
    webLoop();
    delay(1);
}
