/*
  ESP32 HoverKart — Control + Telemetry Web Interface
  -----------------------------------------------
  - Pedal sampling with averaging & smoothing
  - 3 hoverboards (tank mode) via UART0,1,2
  - JSON telemetry + Web Dashboard served from LittleFS
  - OTA + mDNS
*/

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "secrets.h"
#include "tuning.h"

// ========== Structs ==========
typedef struct {
  uint16_t start;
  int16_t cmd1;
  int16_t cmd2;
  int16_t speedR_meas;
  int16_t speedL_meas;
  int16_t batVoltage;
  int16_t boardTemp;
  uint16_t cmdLed;
  uint16_t checksum;
} SerialFeedback;

typedef struct {
  uint16_t start;
  int16_t steer;
  int16_t speed;
  uint16_t checksum;
} SerialCommand;

// ========== Globals ==========
HardwareSerial Hover1(0);
HardwareSerial Hover2(1);
HardwareSerial Hover3(2);
SerialCommand cmdPkt;

SerialFeedback feedback0, feedback1, feedback2;
bool fb0_ok = false, fb1_ok = false, fb2_ok = false;

struct Parser {
  uint8_t buf[sizeof(SerialFeedback)];
  int idx;
};
Parser parser0 = { { 0 }, 0 }, parser1 = { { 0 }, 0 }, parser2 = { { 0 }, 0 };

float filteredCmd = 0.0f;
int16_t rampedCmd = 0;

struct Telemetry {
  int16_t targetCmd;
  int16_t rampedCmd;
  bool fb_ok[3];
  SerialFeedback fb[3];
} telem;

WebServer server(80);

// ========== Helpers ==========
static inline int map_constrain(int x, int in_min, int in_max, int out_min, int out_max) {
  x = constrain(x, in_min, in_max);
  long res = (long)(x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
  return (int)res;
}

void sendToAll(int16_t speed) {
  cmdPkt.start = START_FRAME;
  cmdPkt.steer = speed;
  cmdPkt.speed = speed;
  cmdPkt.checksum = (uint16_t)(cmdPkt.start ^ cmdPkt.steer ^ cmdPkt.speed);
  uint8_t *p = (uint8_t *)&cmdPkt;
  Hover1.write(p, sizeof(cmdPkt));
  Hover2.write(p, sizeof(cmdPkt));
  Hover3.write(p, sizeof(cmdPkt));
}

bool tryParseFeedback(HardwareSerial &serial, Parser &pr, SerialFeedback &outFb) {
  while (serial.available()) {
    uint8_t b = serial.read();
    pr.buf[pr.idx++] = b;
    if (pr.idx >= 2) {
      uint16_t w = ((uint16_t)pr.buf[pr.idx - 2] << 8) | pr.buf[pr.idx - 1];
      if (w == START_FRAME) pr.idx = 2;
    }
    if (pr.idx >= (int)sizeof(SerialFeedback)) {
      SerialFeedback *pf = (SerialFeedback *)pr.buf;
      uint16_t chk = pf->start ^ pf->cmd1 ^ pf->cmd2 ^ pf->speedR_meas ^ pf->speedL_meas ^ pf->batVoltage ^ pf->boardTemp ^ pf->cmdLed;
      if (pf->start == START_FRAME && chk == pf->checksum) {
        outFb = *pf;
        pr.idx = 0;
        return true;
      } else {
        memmove(pr.buf, pr.buf + 1, sizeof(pr.buf) - 1);
        pr.idx--;
      }
    }
  }
  return false;
}

int16_t samplePedalsLerp() {
  int samples = max(1, SAMPLES_PER_CYCLE);
  int interval = (samples > 1) ? (TIME_SEND_MS / samples) : 0;
  long sumT = 0, sumB = 0;

  for (int i = 0; i < samples; i++) {
    int t = analogRead(THROTTLE_PIN);
    int b = analogRead(BRAKE_PIN);
    t = constrain(t, 0, 4095);
    b = constrain(b, 0, 4095);
    sumT += t;
    sumB += b;
    ArduinoOTA.handle();
    if (interval > 2 && i < samples - 1) delay(interval);
  }

  int throttleAvg = sumT / samples;
  int brakeAvg = sumB / samples;
  int thrMap = map_constrain(throttleAvg, THROTTLE_RAW_MIN, THROTTLE_RAW_MAX, 0, 1000);
  int brMap = map_constrain(brakeAvg, BRAKE_RAW_MIN, BRAKE_RAW_MAX, 0, 1000);

  int16_t target = (brMap > BRAKE_OVERRIDE_LIM) ? -brMap : thrMap;
  filteredCmd += LERP_ALPHA * ((float)target - filteredCmd);
  int16_t filtInt = round(filteredCmd);

  if (abs(filtInt) <= DEADZONE) filtInt = 0;
  return filtInt;
}

// ========== HTTP Handlers ==========
void handleTelemetry() {
  StaticJsonDocument<256> doc;
  doc["target"] = telem.targetCmd;
  doc["ramped"] = telem.rampedCmd;

  JsonArray arr = doc.createNestedArray("boards");
  for (int i = 0; i < 3; i++) {
    JsonObject o = arr.createNestedObject();
    o["valid"] = telem.fb_ok[i];
    if (telem.fb_ok[i]) {
      SerialFeedback &f = telem.fb[i];
      o["spdR"] = f.speedR_meas;
      o["spdL"] = f.speedL_meas;
      o["V"] = f.batVoltage;
      o["T"] = f.boardTemp;
    }
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ========== Setup ==========
void setup() {
  Serial.begin(115200);
  Hover1.begin(HOVER_SERIAL_BAUD, SERIAL_8N1, UART0_RX_PIN, UART0_TX_PIN);
  Hover2.begin(HOVER_SERIAL_BAUD, SERIAL_8N1, UART1_RX_PIN, UART1_TX_PIN);
  Hover3.begin(HOVER_SERIAL_BAUD, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);

  analogReadResolution(12);
  pinMode(THROTTLE_PIN, INPUT);
  pinMode(BRAKE_PIN, INPUT);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed!");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

  if (strlen(hostname) > 0) ArduinoOTA.setHostname(hostname);
  ArduinoOTA.begin();

  if (MDNS.begin(hostname)) {
    Serial.println("mDNS responder started");
  }

  server.on("/telemetry", HTTP_GET, handleTelemetry);

  // Serve static files
  server.serveStatic("/", LittleFS, "/");
  
  /**/
  server.on("/", HTTP_GET, []() {
    File file = LittleFS.open("/index.html", "r");
    if (!file) {
      server.send(404, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });
  server.begin();
  Serial.println("HTTP server started");
}

unsigned long nextSend = 0;
void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  if (tryParseFeedback(Hover1, parser0, feedback0)) fb0_ok = true;
  if (tryParseFeedback(Hover2, parser1, feedback1)) fb1_ok = true;
  if (tryParseFeedback(Hover3, parser2, feedback2)) fb2_ok = true;

  unsigned long now = millis();
  if (now >= nextSend) {
    nextSend = now + TIME_SEND_MS;

    int16_t target = samplePedalsLerp();
    if (!RAMP_ENABLED) rampedCmd = target;
    else {
      int d = target - rampedCmd;
      d = constrain(d, -MAX_STEP, MAX_STEP);
      rampedCmd += d;
    }

    sendToAll(rampedCmd);

    telem.targetCmd = target;
    telem.rampedCmd = rampedCmd;
    telem.fb_ok[0] = fb0_ok;
    telem.fb_ok[1] = fb1_ok;
    telem.fb_ok[2] = fb2_ok;
    telem.fb[0] = feedback0;
    telem.fb[1] = feedback1;
    telem.fb[2] = feedback2;
  }
}
