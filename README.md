# AngryESP

Control the power and reset PINs of an ATX mainboard via an ESP32.

## PCB
* prefer not to solder the ESP directly to the board, use sockets
* not all pins on the ESP socket need to be populated, only 2+4 are required
* if the pinout of your ESP does not match, connect the correct pins via jumper cables
* the buttons and the HDD LED are not required
* the power LED is required to drop the 5V of the LED pin to a safe value for the ESP (3.3V)
* connect the 5V and GND header to a USB header on the mainboard
* enable USB charging or Wake up by USB on the UEFI to power the USB ports when the system is off
* the 5V and GND pin rows are for connecting additional things, e.g. fans
* system header pins have an in and an out to allow connecting to the chassis as well
* reset and power button headers have one 3.3V pin, one GND pin, measure the pins to figure out which one is which if your mainboard does not document this

**Verify voltage levels of the buttons before connecting the ESP** The ESP only tolerates 3.3V. Your mainboard might use 5V on the buttons, this might damage the ESP.

Pin mappings for reference:

* GPIO18 - Reset
* GPIO21 - Short Power
* GPIO19 - Long Power
* GPIO5  - Power LED

Long power (6s press) and short power (0.2s press) use two different pins for easier setup in ESPHome. The two pins are shorted on the PCB and go to the power button header.

## ESPHome config
There are two options:
1. Home Assistant integrated
2. Standalone

### Home Assistant integrated
* adds the ESP to HA via the ESPHome integration
* buttons and LED are HA entities
* requires working HA
* control via API of HA

Use `ESPHome/ha.yaml` in the ESPHome Builder in HA. Create a new device and copy the contents of the `ha.yaml` into the file below the auto generated parts of the ESPHome config.

### Standalone
* uses a simple HTTP API on the ESP directly
* control via API of the ESP
* only requires wifi AP with DHCP

TODO
