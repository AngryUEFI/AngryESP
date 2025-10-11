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
The `Standalone/` PlatformIO project ships a self-contained firmware that exposes a direct HTTP API for power control without Home Assistant. 
It supports ESP32 boards (untested) and the Arduino UNO R4 WiFi (tested, working).

* build with `pio run -e esp32doit-espduino` for ESP32 or `pio run -e uno_r4_wifi` for the UNO R4 WiFi
* upload with the matching `--target upload` command once the board is connected
* configure Wi-Fi credentials by creating `Standalone/include/wifi_config.h` before flashing (based on template in same folder)
* configure pins used on the board in `Standalone/include/pins_config.h` and the timing configuration for power cycling the experiment machine.
* HTTP API endpoints:
  * `POST /api/power/on` — tap the ATX power button (short pulse)
  * `POST /api/power/off` — hold the power header long enough for a hard shutdown (~6 s)
  * `POST /api/power/reset` — toggle the reset header
  * `GET /api/power/led` — read the live state of the power LED input
  * `POST /api/system/reboot` — reboot the controller itself
  * `GET /api/health` — Returns SSID details and IP
