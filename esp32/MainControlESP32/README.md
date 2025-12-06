# ESP32 HoverKart Controller & Web Telemetry Dashboard

A full-featured controller and telemetry system for a 6×6 “hoverkart” using hoverboard boards as wheels. The ESP32 acts as the main “brain,” communicating over UART with the hoverboard mainboards, reading throttle/brake pedals, and serving a web dashboard + OTA updates.

---

## Table of Contents

1. Features  
2. Architecture & Data Flow  
3. Wiring & Pinout  
4. Protocol & Packet Format  
5. Configuration & Tuning  
6. Web Dashboard / Telemetry  
7. OTA / mDNS / Remote Update  
8. Safety & Failsafe Behavior  
9. How to Upload / Build  
10. Troubleshooting  
11. Possible Extensions / Next Steps  

---

## 1. Features

- Support for **3 hoverboard mainboards** over UART (each controlling two motors in “tank” mode).  
- Throttle & brake reading from Hall sensor signals (analog) with multi-sampling + interpolation (“lerp”) smoothing.  
- JSON telemetry endpoint (`/telemetry`) and a web dashboard UI served from LittleFS.  
- Dynamic calibration: you can input scaling factors (voltage, temperature, speed) via the dashboard (saved in browser cookies).  
- OTA (Over-the-Air) firmware updates via ArduinoOTA with hostname / mDNS.  
- Modular layout: static file serving, telemetry, network, motor logic.  
- Configurable ramping (slew limiting) to avoid abrupt changes.  
- Board status indicators (active/inactive) on dashboard.  
- Basic safety: when communication fails, motors stop or hold safe value.

---

## 2. Architecture & Data Flow

Pedals (Hall sensors)
↓ sampled many times per cycle → average & smoothing
↓ compute target throttle/brake command
↓ ramp / slew-limit (optional) → rampedCmd
↓ send (steer = speed = rampedCmd) via UART to all 3 hoverboards

hoverboard boards respond via UART → feedback packets:
speedR_meas, speedL_meas, batVoltage, boardTemp, etc.

ESP32:

Parses incoming feedback streams for all 3 boards

Maintains “latest valid” feedback

Exposes a telemetry struct combining command + feedback

Hosts web server: serves dashboard files & telemetry JSON

Responds to OTA update requests

- The dashboard polls `/telemetry` periodically (e.g. every 0.5–1 s) and updates the UI.  
- The configuration tab allows user calibration of scaling factors (e.g. how raw voltage readings map to real voltage).  
- All files (`index.html`, `style.css`, `script.js`) are stored in `data/` and uploaded to LittleFS.

---

## 3. Wiring & Pinout

ESP32 Hoverboard Mainboards (3 separate)
UART0 (remapped) RX ↔ board #1 TX
UART0 → board #1 RX ↔ TX0

UART1 RX ↔ board #2 TX
UART1 TX ↔ board #2 RX

UART2 RX ↔ board #3 TX
UART2 TX ↔ board #3 RX

GND (common) → all boards

Throttle hall sensor → analog pin (e.g. GPIO34)
Brake hall sensor → analog pin (e.g. GPIO35)
3.3 V supply → sensors
GND → sensors

- Make sure all grounds (ESP32 + boards + sensors) are common.  
- Use appropriate voltage tolerance: hoverboard’s USART3 is 5 V tolerant, USART2 is **not** 5 V tolerant. :contentReference[oaicite:0]{index=0}  
- Use shielded or twisted-pair wires for UART and sensor lines if noisy.  
- The wiring of TX/RX directions: ESP32 TX → board RX, ESP32 RX → board TX.

---

## 4. Protocol & Packet Format

The communication with the hoverboard boards uses a binary packet format:

### Command Packet (`SerialCommand`)

| Field     | Type       | Description                            |
|----------|------------|----------------------------------------|
| start    | uint16_t   | Start marker (e.g. `0xABCD`)           |
| steer    | int16_t    | Steering (speed) value                 |
| speed    | int16_t    | Speed (same as steer in tank mode)     |
| checksum | uint16_t   | XOR of start, steer, speed             |

- We send steer = speed in “tank” mode since each board uses both motors together.  
- The checksum helps detect packet corruption.

### Feedback Packet (`SerialFeedback`)

| Field        | Type       | Description                              |
|--------------|------------|------------------------------------------|
| start        | uint16_t   | Start marker                              |
| cmd1         | int16_t    | Echo / command part                       |
| cmd2         | int16_t    | Echo / command part                       |
| speedR_meas  | int16_t    | Measured RPM or speed right motor         |
| speedL_meas  | int16_t    | Measured RPM or speed left motor          |
| batVoltage   | int16_t    | Raw voltage or battery reading            |
| boardTemp    | int16_t    | Raw temperature reading                   |
| cmdLed       | uint16_t   | LED command / state                       |
| checksum     | uint16_t   | XOR of the above fields                   |

Parsing is done in a sliding buffer approach: if the last two bytes form the start marker, reset buffer, accumulate until full size, then validate checksum. If invalid, shift buffer and retry.  

---

## 5. Configuration & Tuning

### In `tuning.h`

You should define (or tune) constants:

```c
#define THROTTLE_PIN     <pin>
#define BRAKE_PIN        <pin>
#define THROTTLE_RAW_MIN <raw_min>
#define THROTTLE_RAW_MAX <raw_max>
#define BRAKE_RAW_MIN    <raw_min>
#define BRAKE_RAW_MAX    <raw_max>

#define TIME_SEND_MS       <e.g. 100>
#define SAMPLES_PER_CYCLE  <e.g. 10>
#define LERP_ALPHA         <0.0-1.0>
#define DEADZONE           <int threshold>
#define BRAKE_OVERRIDE_LIM <value over which brake overrides throttle>

#define RAMP_ENABLED       1
#define MAX_STEP           <max delta per cycle>
Throttle / brake scaling: capture raw min/max by logging analog readings with your hardware.

Alpha (lerp factor): lower = smoother, higher = snappier.

Deadzone: small region around zero where input is treated as zero to avoid jitter.

Slewing / ramping: prevents jerkiness by limiting how fast command can change.

Scaling factors on dashboard: use the config tab to tune VOLTAGE_SCALE, TEMP_SCALE, TEMP_OFFSET, SPEED_SCALE.

6. Web Dashboard / Telemetry
/telemetry JSON Structure
An example output:

json
Copy code
{
  "target": 200,
  "ramped": 150,
  "boards": [
    { "valid": true, "spdR": 1234, "spdL": 1220, "V": 370, "T": 50 },
    { "valid": false },
    { "valid": true, "spdR": 1200, "spdL": 1190, "V": 368, "T": 48 }
  ]
}
target: raw target command from pedals

ramped: possibly ramp-limited command

boards[]: per hoverboard feedback

valid: true if feedback packet was successfully parsed recently

If valid, then fields spdR, spdL, V, T are present

Dashboard UI
Polls /telemetry (e.g. every 500 ms or 1 s).

Shows:

Speed computed from average RPMs (using SPEED_SCALE)

Battery % derived from raw V * VOLTAGE_SCALE

Temperature gauges (thermometers) for each board, mapped over 0–100 °C (or your range)

Hoverboard active indicators (green / red)

Config tab: set scaling constants stored as browser cookies

Theme selector and responsive layout

7. OTA / mDNS / Remote Update
Uses ArduinoOTA to support wireless firmware updates.

ESPmDNS.begin(hostname) advertises the device via .local name on the local network.

After initial USB upload, subsequent uploads can be done over the network using the hostname.local or IP.

OTA conflicts: avoid using the same UART (UART0) for debugging during OTA; remap UART0 exclusively for hoverboard comms.

8. Safety & Failsafe Behavior
If feedback parsing fails (no valid feedback for a board), that board is flagged inactive (dashboard indicator).

You should program fallback logic in code (e.g. do not send commands, or send zero) if all boards are invalid.

Use physical emergency stop (cut power) always available.

In configuration tuning, avoid allowing extreme commands (e.g. full throttle) by mistake.

The start marker + checksum in the packet helps detect data corruption.

Ramping ensures that throttle changes are gradual and safer.

9. How to Upload / Build
Pre-requisites
Arduino IDE (or PlatformIO) with ESP32 support

LittleFS / SPIFFS uploader (for file system)

secrets.h file containing:

const char* ssid = "...";
const char* password = "...";
const char* hostname = "hoverkart";
tuning.h with calibration constants

Steps
Place index.html, style.css, script.js into data/ folder next to your .ino

Compile and upload sketch via USB (first time)

Use Tools → ESP32 Sketch Data Upload (or equivalent) to upload the data/ folder into LittleFS

Open Serial Monitor at 115200 to see logs (WiFi IP, mDNS, etc.)

Browse to http://<esp_ip>/ or http://<hostname>.local/

Use OTA: in Arduino IDE, select network port HoverKart at <IP> and upload

Observe telemetry dashboard, verify communications and motor response

10. Troubleshooting
Symptom	Possible Cause	Debug / Fix
404 Not Found for /telemetry	Handler not registered or wrong path	Ensure server.on("/telemetry", HTTP_GET, handleTelemetry) is before server.begin()
index.html shows, but script.js missing	File name typo, or not uploaded to LittleFS	Add explicit handlers or list LittleFS content via Serial to verify
Telemetry JSON empty or invalid	No feedback parsed	Verify UART wiring, check parity, packet format
Motors don’t respond	Commands not being sent, or hoverboard firmware mismatch	Send test packets, verify hoverboard firmware expects same protocol
OTA fails / undefined MDNS	Missing #include <ESPmDNS.h>	Ensure you include ESPmDNS.h before ArduinoOTA.begin()
Erratic pedal readings	Electrical noise or jitter	Use smoothing, increase sample count, hardware filtering (capacitors)

To list files in LittleFS for debugging, insert:

Serial.println("LittleFS contents:");
Dir dir = LittleFS.openDir("/");
while (dir.next()) {
  Serial.println(dir.fileName());
}
11. Possible Extensions / Next Steps
Graphing / Historical Data: in dashboard, render line charts (temperature over time, speed vs. time).

Control modes: experiment with sending different steer / multi-wheel differential commands (not just tank = same).

Command feedback: extend telemetry to include battery current, error codes, etc.

Mobile interface / app: wrap the dashboard in a PWA or native app.

Safety features: e.g. battery undervoltage shutdown, overtemp limits, active braking commands.

Sensor fusion / odometry: use motor encoders + IMU to compute position / path tracking.

Bluetooth / radio fallback: allow remote control fallback if WiFi fails.

Remote config persistence: store calibration constants on ESP32 (SPIFFS) instead of cookies.