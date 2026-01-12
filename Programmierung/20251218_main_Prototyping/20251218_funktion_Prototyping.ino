/*
 * functions.ino
 * All logic: ESC control, BLE UART, command parser, logging.
 *
 * Commands (Serial or BLE UART):
 *   HELP
 *   ARM
 *   STOP
 *   FULL
 *   SET <0..100>        (target percent)
 *   RPM <value>         (target RPM, estimated from KV and battery voltage)
 *   US <1000..2000>     (direct pulse width debug; still forward-only clamped)
 *   RAMP <ms>           (0 = immediate, e.g. 300 for soft start)
 *   DBG ON | DBG OFF
 *   LOG START | LOG STOP
 *   LOG PERIOD <ms>     (e.g. 100, 200, 1000)
 *   STATUS
 *   LED ON | LED OFF
 *
 * Control Pad (Bluefruit):
 *   "!B<id><state>" (we act on state==1 press events)
 */

#include <Arduino.h>
#include <ESP32Servo.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <string>

// ============================================================
//                      USER CONFIG
// ============================================================

// Some cores do not define LED_BUILTIN
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

static const int LED_PIN = LED_BUILTIN;

// ESC signal pin (must be a valid GPIO on your FireBeetle ESP32)
static const int PIN_ESC_SIGNAL = 16;

// BLE UUIDs (Bluefruit UART compatible)
static const char* SERVICE_UUID           = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* CHARACTERISTIC_UUID_RX = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // Phone -> ESP32
static const char* CHARACTERISTIC_UUID_TX = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // ESP32 -> Phone

// Control Pad mapping (adjust if needed)
static const int BTN_FULL_ID = 1;  // button "1"
static const int BTN_STOP_ID = 2;  // button "2"
static const int BTN_UP_ID   = 5;  // up arrow
static const int BTN_DOWN_ID = 6;  // down arrow

// ============================================================
//              MOTOR MODEL (for RPM estimate/debug)
// ============================================================
//
// RS-540PH-7021: ~20000 rpm @ 7.2 V no-load (datasheet).
// KV estimate: 20000/7.2 ≈ 2777.8 rpm/V (no-load).
static const float MOTOR_KV_RPM_PER_V = 20000.0f / 7.2f;

// Assume 2S LiPo: nominal 7.4 V, full 8.4 V. Use a configurable value:
static float g_battVoltage_V = 8.4f; // for "maximum RPM" estimate

// ============================================================
//                       ESC CONFIG
// ============================================================

struct EscConfig {
  int pinSignal   = PIN_ESC_SIGNAL;
  int usMin       = 1000; // physical min of servo pulse range
  int usMax       = 2000; // physical max of servo pulse range
  int usNeutral   = 1500; // neutral / stop
  int usMinFwd    = 1520; // forward starts here (forward-only policy)
  int usMaxFwd    = 2000; // full forward
};

static EscConfig g_escCfg;

// ============================================================
//                       RUNTIME STATE
// ============================================================

static Servo g_esc;

static bool     g_deviceConnected = false;
static bool     g_armed           = false;

static uint8_t  g_targetPct       = 0;   // desired throttle 0..100
static float    g_outputPct       = 0.0; // actual output after ramping

static uint32_t g_lastLoopMs      = 0;
static uint32_t g_lastInputMs     = 0;

// ramping: time to go 0->100% (ms)
static uint32_t g_rampTimeMs      = 300;

// debug/log
static bool     g_debugEnabled    = true;
static bool     g_logEnabled      = false;
static uint32_t g_logPeriodMs     = 200;
static uint32_t g_lastLogMs       = 0;

// ============================================================
//                       BLE OBJECTS
// ============================================================

static BLECharacteristic* g_txCharacteristic = nullptr;

// ============================================================
//                       UTILITIES
// ============================================================

static int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static float clampFloat(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Send to Serial + BLE (if connected)
static void logLine(const String& line) {
  Serial.println(line);

  if (g_deviceConnected && g_txCharacteristic) {
    g_txCharacteristic->setValue(line.c_str());
    g_txCharacteristic->notify();
  }
}

static void logCsv(const String& tag) {
  // CSV: t_ms,tag,connected,armed,targetPct,outputPct,pulse_us,rpm_est
  const uint32_t t = millis();
  const int pulse = 0; // filled later by motorPulseFromPct (we compute inside)
  (void)pulse;

  // We'll compute pulse and rpm in-place:
  // (done below for correctness)
}

// ============================================================
//                 ESC / MOTOR LOW LEVEL
// ============================================================

static int escClampForwardOnly(int us) {
  us = clampInt(us, g_escCfg.usMin, g_escCfg.usMax);

  // forward-only: anything below minFwd becomes neutral (stop)
  if (us < g_escCfg.usMinFwd) us = g_escCfg.usNeutral;
  if (us > g_escCfg.usMaxFwd) us = g_escCfg.usMaxFwd;

  return us;
}

static void escWriteMicrosecondsSafe(int us) {
  us = escClampForwardOnly(us);
  g_esc.writeMicroseconds(us);
}

static int escPulseFromPercent(uint8_t pct) {
  pct = (uint8_t)clampInt((int)pct, 0, 100);

  if (pct == 0) return g_escCfg.usNeutral;

  // Map 1..100% to minFwd..maxFwd
  const int span = g_escCfg.usMaxFwd - g_escCfg.usMinFwd;
  const int us = g_escCfg.usMinFwd + (span * (int)pct) / 100;
  return escClampForwardOnly(us);
}

static float estimateRpmFromPercent(float pct) {
  pct = clampFloat(pct, 0.0f, 100.0f);
  const float rpmNoLoad = MOTOR_KV_RPM_PER_V * g_battVoltage_V;
  return rpmNoLoad * (pct / 100.0f);
}

static uint8_t percentFromTargetRpm(float rpmTarget) {
  if (rpmTarget <= 0) return 0;
  const float rpmNoLoad = MOTOR_KV_RPM_PER_V * g_battVoltage_V;
  if (rpmNoLoad <= 1.0f) return 0;

  float pct = 100.0f * (rpmTarget / rpmNoLoad);
  pct = clampFloat(pct, 0.0f, 100.0f);
  return (uint8_t)(pct + 0.5f);
}

static void escArmNeutral(uint32_t msHold) {
  escWriteMicrosecondsSafe(g_escCfg.usNeutral);
  const uint32_t t0 = millis();
  while (millis() - t0 < msHold) {
    delay(10);
  }
  g_armed = true;
}

// ============================================================
//                   THROTTLE STATE UPDATE
// ============================================================

static void setTargetPercent(uint8_t pct) {
  g_targetPct = (uint8_t)clampInt((int)pct, 0, 100);
  g_lastInputMs = millis();
}

static void stopMotor() {
  g_targetPct = 0;
  g_outputPct = 0.0f;
  escWriteMicrosecondsSafe(g_escCfg.usNeutral);
  g_lastInputMs = millis();
}

static void applyThrottleWithRamping() {
  const uint32_t now = millis();
  const uint32_t dt  = (g_lastLoopMs == 0) ? 0 : (now - g_lastLoopMs);
  g_lastLoopMs = now;

  if (!g_armed) {
    escWriteMicrosecondsSafe(g_escCfg.usNeutral);
    g_outputPct = 0.0f;
    return;
  }

  // If ramp disabled or dt==0 -> immediate
  if (g_rampTimeMs == 0 || dt == 0) {
    g_outputPct = (float)g_targetPct;
  } else {
    // Max percent change allowed in this dt
    const float maxDelta = 100.0f * ((float)dt / (float)g_rampTimeMs);
    const float target = (float)g_targetPct;
    const float delta  = target - g_outputPct;

    if (fabs(delta) <= maxDelta) {
      g_outputPct = target;
    } else {
      g_outputPct += (delta > 0) ? maxDelta : -maxDelta;
    }
  }

  g_outputPct = clampFloat(g_outputPct, 0.0f, 100.0f);

  const int pulse = escPulseFromPercent((uint8_t)(g_outputPct + 0.5f));
  escWriteMicrosecondsSafe(pulse);
}

// ============================================================
//                     COMMAND PARSING
// ============================================================

static void printHelp() {
  logLine("HELP: ARM, STOP, FULL, SET <0..100>, RPM <val>, US <1000..2000>, RAMP <ms>, DBG ON|OFF, LOG START|STOP, LOG PERIOD <ms>, STATUS, LED ON|OFF");
}

static void printStatus(const String& reason) {
  const int pulse = escPulseFromPercent((uint8_t)(g_outputPct + 0.5f));
  const float rpmEst = estimateRpmFromPercent(g_outputPct);

  String s;
  s.reserve(200);
  s += "STATUS [" + reason + "] ";
  s += "conn=" + String(g_deviceConnected ? "1" : "0");
  s += " armed=" + String(g_armed ? "1" : "0");
  s += " targetPct=" + String(g_targetPct);
  s += " outPct=" + String(g_outputPct, 1);
  s += " pulse_us=" + String(pulse);
  s += " rpm_est=" + String(rpmEst, 0);
  s += " ramp_ms=" + String(g_rampTimeMs);
  s += " log=" + String(g_logEnabled ? "1" : "0");
  logLine(s);
}

static void logCsvNow(const String& tag) {
  const uint32_t t = millis();
  const int pulse = escPulseFromPercent((uint8_t)(g_outputPct + 0.5f));
  const float rpmEst = estimateRpmFromPercent(g_outputPct);

  // t_ms,tag,connected,armed,targetPct,outputPct,pulse_us,rpm_est
  String line;
  line.reserve(180);
  line += String(t);
  line += ",";
  line += tag;
  line += ",";
  line += (g_deviceConnected ? "1" : "0");
  line += ",";
  line += (g_armed ? "1" : "0");
  line += ",";
  line += String(g_targetPct);
  line += ",";
  line += String(g_outputPct, 2);
  line += ",";
  line += String(pulse);
  line += ",";
  line += String(rpmEst, 0);

  logLine(line);
}

static void handleTextCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  // Control Pad packets: "!B<id><state>"
  if (cmd.startsWith("!B") && cmd.length() >= 4) {
    const int btn   = cmd.charAt(2) - '0';
    const int state = cmd.charAt(3) - '0';

    if (g_debugEnabled) {
      Serial.print("CPAD raw=");
      Serial.print(cmd);
      Serial.print(" btn=");
      Serial.print(btn);
      Serial.print(" state=");
      Serial.println(state);
    }

    if (state != 1) return; // only press

    if (btn == BTN_UP_ID) {
      const int next = clampInt((int)g_targetPct + 5, 0, 100);
      setTargetPercent((uint8_t)next);
      printStatus("CPAD_UP");
      return;
    }
    if (btn == BTN_DOWN_ID) {
      const int next = clampInt((int)g_targetPct - 5, 0, 100);
      setTargetPercent((uint8_t)next);
      printStatus("CPAD_DOWN");
      return;
    }
    if (btn == BTN_FULL_ID) {
      setTargetPercent(100);
      printStatus("CPAD_FULL");
      return;
    }
    if (btn == BTN_STOP_ID) {
      stopMotor();
      printStatus("CPAD_STOP");
      return;
    }
    logLine("CPAD: unknown button");
    return;
  }

  String upper = cmd;
  upper.toUpperCase();

  if (upper == "HELP") { printHelp(); return; }

  if (upper == "LED ON")  { digitalWrite(LED_PIN, HIGH); logLine("LED ON"); return; }
  if (upper == "LED OFF") { digitalWrite(LED_PIN, LOW);  logLine("LED OFF"); return; }

  if (upper == "ARM") {
    escArmNeutral(3000);
    stopMotor();
    printStatus("ARMED");
    return;
  }

  if (upper == "STOP" || upper == "0") {
    stopMotor();
    printStatus("STOP");
    return;
  }

  if (upper == "FULL" || upper == "1") {
    setTargetPercent(100);
    printStatus("FULL");
    return;
  }

  if (upper == "DBG ON")  { g_debugEnabled = true;  logLine("DBG ON");  return; }
  if (upper == "DBG OFF") { g_debugEnabled = false; logLine("DBG OFF"); return; }

  if (upper == "LOG START") { g_logEnabled = true;  logLine("LOG START (CSV)"); return; }
  if (upper == "LOG STOP")  { g_logEnabled = false; logLine("LOG STOP");        return; }

  if (upper == "STATUS") { printStatus("MANUAL"); return; }

  // Parameter commands with values:
  // SET <0..100>
  if (upper.startsWith("SET ")) {
    const int v = cmd.substring(4).toInt();
    setTargetPercent((uint8_t)clampInt(v, 0, 100));
    printStatus("SET");
    return;
  }

  // RPM <value>
  if (upper.startsWith("RPM ")) {
    const float rpm = cmd.substring(4).toFloat();
    const uint8_t pct = percentFromTargetRpm(rpm);
    setTargetPercent(pct);
    printStatus("RPM");
    return;
  }

  // US <1000..2000>  (debug direct pulse; still forward-only clamped)
  if (upper.startsWith("US ")) {
    const int us = cmd.substring(3).toInt();
    escWriteMicrosecondsSafe(us);
    // In this debug mode we also update targets to reflect roughly:
    // (map back just for status readability)
    g_outputPct = (float)g_targetPct; // keep ramp logic consistent
    printStatus("US");
    return;
  }

  // RAMP <ms>
  if (upper.startsWith("RAMP ")) {
    const int ms = cmd.substring(5).toInt();
    g_rampTimeMs = (uint32_t)clampInt(ms, 0, 10000);
    printStatus("RAMP");
    return;
  }

  // LOG PERIOD <ms>
  if (upper.startsWith("LOG PERIOD ")) {
    const int ms = cmd.substring(11).toInt();
    g_logPeriodMs = (uint32_t)clampInt(ms, 20, 10000);
    printStatus("LOGPER");
    return;
  }

  logLine("Unknown cmd: " + cmd);
}

// ============================================================
//                       BLE CALLBACKS
// ============================================================

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    (void)pServer;
    g_deviceConnected = true;
    Serial.println("BLE: connected");
    printStatus("BLE_CONNECT");
  }

  void onDisconnect(BLEServer* pServer) override {
    (void)pServer;
    g_deviceConnected = false;
    Serial.println("BLE: disconnected, advertising again...");
    BLEDevice::startAdvertising();
    printStatus("BLE_DISCONNECT");
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    // In your BLE library, getValue() returns Arduino String
    String cmd = pCharacteristic->getValue();
    cmd.trim();
    if (cmd.length() == 0) return;

    if (g_debugEnabled) {
      Serial.print("BLE RX: ");
      Serial.println(cmd);
    }

    handleTextCommand(cmd);
  }
};


// ============================================================
//                         APP SETUP/LOOP
// ============================================================

void appSetup() {
  Serial.begin(115200);
  delay(300);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // ESC init
  g_esc.attach(g_escCfg.pinSignal, g_escCfg.usMin, g_escCfg.usMax);
  escWriteMicrosecondsSafe(g_escCfg.usNeutral);
  delay(200);

  // BLE init
  BLEDevice::init("FireBeetle_ESC_UART");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new MyServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);

  g_txCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  g_txCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic* rxCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  rxCharacteristic->setCallbacks(new RxCallbacks());

  service->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Ready: BLE UART + Control Pad + Serial");
  printHelp();

  // Arm at neutral (required by many ESCs)
  escArmNeutral(3000);
  stopMotor();
  printStatus("BOOT");
}

void appLoop() {
  // Serial command input
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) handleTextCommand(cmd);
  }

  // Apply throttle each loop (with ramping)
  applyThrottleWithRamping();

  // Periodic CSV logging
  if (g_logEnabled) {
    const uint32_t now = millis();
    if (now - g_lastLogMs >= g_logPeriodMs) {
      logCsvNow("LOG");
      g_lastLogMs = now;
    }
  }

  delay(5);
}
