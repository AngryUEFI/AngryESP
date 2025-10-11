# AngryESP Standalone Controller Setup

This guide walks through the hardware and firmware steps needed to bring up the standalone AngryESP controller on either an ESP32 development board or an Arduino Uno R4 WiFi. This will walk through the setup of ATX motherboard header wiring, Wi-Fi credentials, and flashing of the firmware.

## What You Need

- One supported control board:
  - ESP32 DevKit (requires the PCB used in AngryESP for proper voltage levels, as the ESP32 is not 5 V tolerant)
  - Arduino Uno R4 WiFi (fully tested, 5 V tolerant I/O)
- Dupont leads or a custom harness that mates with the ATX front-panel header block
- USB-C or micro-USB cable for programming the board
- PlatformIO installed either through standalone CLI or your favorite IDE extension
- Local 2.4 GHz Wi-Fi network

> ⚠️ Always confirm that the motherboard header never drives more than 3.3 V into ESP32 pins. Use a multimeter to verify wiring before connecting the board. 
>
> My ASUS B450M-A II uses 5V, which will damage the ESP32. The Uno R4 WiFi is 5V tolerant, so it can be connected directly.

## Hardware Wiring Overview

| ATX Signal                 | ESP32 Pin | Uno R4 WiFi Pin | Notes                                           |
|----------------------------|-----------|-----------------|-------------------------------------------------|
| Power Switch (short press) | `GPIO21` | `D11`           | Active LOW pulse triggers power on/off          |
| Power Switch (long press)  | `GPIO19` | N/A             | The ESP32 PCB merges these; Uno R4 reuses `D11` |
| Reset Switch               | `GPIO18` | `D12`           | Active LOW pulse                                |
| Power LED +                | `GPIO5`  | `D10`           | Read-only input                                 |
| Power LED -                | `GND` | `GND`             | Or any other exposed ground on the motherboard. |

- For ESP32 / Uno R4 WiFi boards that expose open-drain capability, the firmware drives the control outputs as open-drain with pull-ups handled on the PC side.
- For other boards, the firmware toggles the pin direction to emulate open-drain.
- This hijacks the motherboard's power and reset buttons, you can no longer use them directly without custom wiring.

## Firmware Preparation

1. **Clone the repository**
   ```bash
   git clone https://github.com/BMorgan1296/AngryESP-standalone.git
   cd AngryESP-standalone/Standalone
   ```

2. **Install PlatformIO** (if you have not already)
   ```bash
   pip install platformio  # or use your preferred IDE extension
   ```

3. **Set Wi-Fi credentials**
   - Copy the template: `cp include/wifi_config_example.h include/wifi_config.h`
   - Edit `include/wifi_config.h` and set:
     ```c++
     #define WIFI_SSID "YourNetwork"
     #define WIFI_PASSWORD "SuperSecret"
     #define WIFI_CONNECT_TIMEOUT_MS 15000
     #define WIFI_RETRY_INTERVAL_MS 50
     ```
   - Keep this file out of version control if you fork the project (`.gitignore` already covers it).

4. **Select the target board**
   - `platformio.ini` already contains `esp32doit-espduino` and `uno_r4_wifi` environments.
   - ESP32 is the default; specify the Uno environment when you build or upload.

## Building and Flashing

- **Compile**
  - ESP32: `pio run -e esp32doit-espduino`
  - Uno R4 WiFi: `pio run -e uno_r4_wifi`
- **Upload firmware**
  - Connect the board over USB. For ESP32 DevKit, hold `BOOT` while connecting if needed.
  - ESP32: `pio run -e esp32doit-espduino --target upload`
  - Uno R4 WiFi: `pio run -e uno_r4_wifi --target upload`
- **Clean build artifacts** (optional): `pio run -t clean`

## First Boot Validation

1. Open a serial monitor at 115200 baud (`pio device monitor --baud 115200`).
2. Wait for messages similar to:
   ```
    AngryESP Standalone Control booting...
    Power control outputs ready
    Connecting to WiFi... Connected!
    HTTP server started
    Web Server IP address: http://192.168.1.123
    Setup complete.
   ```
3. Note the printed IP address.

## Using the HTTP Interface

- Browser UI: visit `http://<board-ip>/` for a simple control console with buttons and status indicators.
- REST API endpoints:
  - `POST /api/power/on`
  - `POST /api/power/off`
  - `POST /api/power/reset`
  - `GET /api/power/led` (returns JSON with LED state and last change time)
  - `POST /api/system/reboot` (queues a microcontroller reboot ~1 s later and returns 202 while the board finishes the request)
  - `GET /api/health` (basic status payload)

Example command-line usage:
```bash
curl -X POST http://<board-ip>/api/power/on
curl http://<board-ip>/api/power/led
```

Or in Python:
```python
import requests
response = requests.post("http://<board-ip>/api/power/on")
print(response.json())
```

Response example:
```json
{'status': 'ok', 'action': 'Power On', 'power_state': {'state': 'on', 'since': '00:00:00', 'last_action': 'Power On', 'last_action_age': '00:00:00'}}
```

## Troubleshooting
- **Power actions do nothing**: verify wiring to the ATX headers and confirm that the control pins pull the line LOW when triggered. You also need to ensure the power/reset lines are connected to the correct pin, as out of the two, one is GND on each. You also cannot put use pin `D13` on the Uno R4 WiFi, as it is hooked up to an LED which will interfere with the signal. 
- **Wi-Fi connection issues**: double-check SSID and password, ensure 2.4 GHz network, and verify signal strength, the usual.
- **Firmware upload fails**: ensure correct board selected, drivers installed, and proper USB cable, as always.
