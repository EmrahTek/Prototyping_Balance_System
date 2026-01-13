# FireBeetle ESP32 Reaction-Wheel Motor Bring-Up (THW-1060 + RS-540PH-7021)
**BLE UART (Bluefruit/Nordic compatible) + Serial Command Interface + Safe Forward-Only Throttle**

> **EN:** This firmware is the **actuator bring-up / calibration** block for a future reaction-wheel balancing system.  
> **DE:** Diese Firmware ist der **Aktoren-/Kalibrier-Teil** für ein späteres Reaktionsrad-Balancingsystem.

---

## Table of Contents / Inhaltsverzeichnis
- [EN — English](#en)
  - [1. Overview](#en-overview)
  - [2. Features](#en-features)
  - [3. Safety Notes (Read First)](#en-safety)
  - [4. Hardware](#en-hardware)
  - [5. Wiring](#en-wiring)
  - [6. Software Setup (Arduino IDE)](#en-software)
  - [7. Build & Upload](#en-upload)
  - [8. How to Use](#en-use)
    - [8.1 Serial Commands](#en-serial)
    - [8.2 BLE UART (Phone App)](#en-ble)
    - [8.3 Logging (CSV)](#en-logging)
  - [9. Configuration](#en-config)
  - [10. Troubleshooting](#en-troubleshooting)
  - [11. Roadmap](#en-roadmap)
  - [12. References](#en-refs)
- [DE — Deutsch](#de)
  - [1. Überblick](#de-ueberblick)
  - [2. Features](#de-features)
  - [3. Sicherheitshinweise (zuerst lesen)](#de-sicherheit)
  - [4. Hardware](#de-hardware)
  - [5. Verdrahtung](#de-verdrahtung)
  - [6. Software Setup (Arduino IDE)](#de-software)
  - [7. Build & Upload](#de-upload)
  - [8. Bedienung](#de-bedienung)
    - [8.1 Serielle Kommandos](#de-serial)
    - [8.2 BLE UART (Handy-App)](#de-ble)
    - [8.3 Logging (CSV)](#de-logging)
  - [9. Konfiguration](#de-konfig)
  - [10. Fehlersuche](#de-fehlersuche)
  - [11. Ausblick](#de-ausblick)
  - [12. Referenzen](#de-referenzen)

---

<a id="en"></a>
# EN — English

<a id="en-overview"></a>
## 1. Overview
This repository contains Arduino firmware for a **FireBeetle ESP32** that controls a brushed DC motor (**RS-540**) through a brushed ESC (**THW-1060 / “1060 class”**) using **RC servo PWM**.

You can control the motor using:
- **USB Serial** commands (Arduino Serial Monitor), and
- **Bluetooth Low Energy UART** (compatible with Adafruit Bluefruit LE Connect / Nordic UART style apps).

This firmware is intended as a **safe, testable motor bring-up** step before adding IMU-based feedback control for balancing.

---

<a id="en-features"></a>
## 2. Features
- **Forward-only motor policy (safety):**  
  Any PWM pulse **below** the forward threshold is clamped to **neutral/stop** (no reverse).
- **ESC arming at neutral:**  
  On boot, the firmware holds **neutral for ~3 seconds**, then sets motor to stop.
- **Two control paths:**
  - Serial (USB) command interface
  - BLE UART command interface
- **Soft-start / ramp:** `RAMP <ms>` controls how quickly output changes.
- **Status output:** `STATUS` prints internal state and output values.
- **CSV logging:** periodic lines you can paste into Excel/Python for plotting.

---

<a id="en-safety"></a>
## 3. Safety Notes (Read First)
This system can be dangerous. Treat it like a power tool.

1. **Secure the motor and wheel** firmly (clamp/enclosure). Wear safety glasses.
2. Use **high-current capable wiring** and connectors.
3. Keep hands/cables away from rotating parts.
4. Start with **low throttle** (`SET 5`) and increase slowly.
5. Have an emergency stop: `STOP` (or disconnect power safely).

> RS-540 class motors can draw very high stall currents. A blocked rotor can overheat ESC/wires quickly.

---

<a id="en-hardware"></a>
## 4. Hardware
**Required**
- FireBeetle ESP32 (ESP32-WROOM class)
- Brushed ESC (THW-1060 / 1060 class, servo PWM input)
- Brushed DC motor: RS-540PH-7021
- Power source: 2S LiPo (7.4–8.4 V) or equivalent

**Optional / recommended**
- Reaction wheel + protective cover
- Inline fuse / switch
- Bench supply with current limit for early tests

---

<a id="en-wiring"></a>
## 5. Wiring

### 5.1 Minimum wiring
- **ESC signal** → ESP32 **GPIO 16**
- **ESC GND** → ESP32 **GND**  
  ✅ Common ground is mandatory.
- **ESC motor outputs** → RS-540 motor terminals
- **Battery** → ESC battery input (observe polarity!)

**ASCII**
```text
Battery -> ESC -> Motor
ESC signal -> ESP32 GPIO16
ESC GND    -> ESP32 GND
```

### 5.2 ESC “red wire” / BEC note
Some ESCs provide 5V BEC on the servo cable (red wire).  
If ESP32 is powered by USB, you usually **do not need** this 5V line.

⚠️ Do **not** connect 5V to a 3.3V pin.

---

<a id="en-software"></a>
## 6. Software Setup (Arduino IDE)

### 6.1 Install ESP32 board support
1. Arduino IDE → **Preferences** → add ESP32 board manager URL (Espressif Arduino-ESP32)
2. Tools → Board → Boards Manager → install **“ESP32 by Espressif Systems”**
3. Select a compatible board (often **ESP32 Dev Module** works)

### 6.2 Install libraries
Arduino IDE → Tools → Manage Libraries:
- **ESP32Servo**

BLE headers are typically included with Arduino-ESP32.

---

<a id="en-upload"></a>
## 7. Build & Upload

### 7.1 Sketch structure (important!)
Arduino compiles all `.ino` files in the **same sketch folder**.

Recommended:
```text
Prototyping_ESC_UART/
  Prototyping_ESC_UART.ino   (main)
  functions.ino              (helper functions)
```

If your files are currently named like:
- `20251218_main_Prototyping.ino`
- `20251218_funktion_Prototyping.ino`

Put them into one folder and rename the **main** file to match the folder name.

### 7.2 Upload steps
1. Connect ESP32 via USB
2. Tools → Port → select correct port
3. Upload
4. Open Serial Monitor at **115200 baud**
5. You should see a “ready/help” output on boot

---

<a id="en-use"></a>
## 8. How to Use

<a id="en-serial"></a>
### 8.1 Serial Commands
Open Serial Monitor (115200) and send newline-terminated commands.

**Core commands**
- `HELP` — print command list
- `STATUS` — print state (connected, armed, target %, output %, pulse, rpm estimate)
- `STOP` — immediate neutral/stop
- `SET <0..100>` — set target throttle percent
- `RAMP <ms>` — set ramp time (0 = instant)
- `LOG START` / `LOG STOP` — start/stop CSV logging
- `LOG PERIOD <ms>` — set logging period, e.g. `LOG PERIOD 100`

**Debug commands (use carefully)**
- `US <1000..2000>` — direct PWM pulse (still forward-only clamped)
- `RPM <value>` — open-loop mapping to estimated RPM model

**Safe first test (recommended)**
1. Power ESP32 via USB
2. Secure motor/wheel, then power ESC from battery
3. In Serial Monitor:
   - `STOP`
   - `RAMP 500`
   - `SET 5`
   - slowly increase: `SET 10`, `SET 15`, …

---

<a id="en-ble"></a>
### 8.2 BLE UART (Phone App)
The firmware advertises as:
- **Device name:** `FireBeetle_ESC_UART`

It implements Nordic/Adafruit UART UUIDs:
- Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX (Phone → ESP32): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX (ESP32 → Phone): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

**Using Adafruit Bluefruit LE Connect**
1. Install Bluefruit LE Connect
2. Connect to `FireBeetle_ESC_UART`
3. Use UART tab to send the same commands as Serial

**Control Pad mapping (if enabled in your code)**
The firmware supports Bluefruit Control Pad style packets (`!B<id><state>`).
Typical mapping:
- Button “1” → FULL (100%)
- Button “2” → STOP
- Up arrow → +5%
- Down arrow → -5%

---

<a id="en-logging"></a>
### 8.3 Logging (CSV)
When enabled, the firmware outputs periodic CSV lines.

**Typical format**
```text
t_ms,tag,connected,armed,targetPct,outputPct,pulse_us,rpm_est
```

Workflow:
1. `LOG PERIOD 100`
2. `LOG START`
3. run a test (SET commands)
4. `LOG STOP`
5. Copy output into a file and plot in Excel/Python

---

<a id="en-config"></a>
## 9. Configuration
Key values are in your configuration structs/constants:

**Pin**
- ESC signal pin: **GPIO 16**

**Servo pulse (microseconds)**
- Neutral: **1500**
- Forward threshold: **~1520**
- Max forward: **2000**

If your ESC requires different values, adjust:
- `usNeutral`, `usMinFwd`, `usMaxFwd`

**Ramp**
- Default ramp time: e.g. `300 ms`
- Set with `RAMP <ms>`

**Battery voltage (for rpm estimate only)**
- Default `8.4 V` (2S full)
- Only affects open-loop `rpm_est` display and `RPM <value>` mapping.

---

<a id="en-troubleshooting"></a>
## 10. Troubleshooting

**Motor does not spin**
- Verify **common ground** (ESC GND ↔ ESP32 GND).
- Ensure ESC is armed/calibrated. Some ESCs require throttle calibration.
- Try `STATUS` then `SET 5`.
- Confirm ESC uses **servo PWM input** (1–2 ms pulses, ~50 Hz).

**Motor starts only at high percent**
- Increase `usMinFwd` slightly (e.g., 1530), OR
- Carefully lower if needed (be cautious).

**BLE not visible**
- Reboot ESP32; ensure not connected from another phone.
- Check phone Bluetooth permissions.

**Output changes too fast**
- Increase ramp: `RAMP 800` or `RAMP 1500`

---

<a id="en-roadmap"></a>
## 11. Roadmap
Next steps toward full balancing:
- IMU integration (MPU6050) for angle estimation
- Feedback control (PID/LQR/state feedback)
- Battery measurement + safety cutoffs
- Telemetry improvements + better log tooling

---

<a id="en-refs"></a>
## 12. References
- Adafruit / Nordic UART service overview:  
  https://learn.adafruit.com/introducing-adafruit-ble-bluetooth-low-energy-friend/uart-service
- ESP32Servo library:  
  https://docs.arduino.cc/libraries/esp32servo/

---

<a id="de"></a>
# DE — Deutsch

<a id="de-ueberblick"></a>
## 1. Überblick
Dieses Repository enthält Arduino-Firmware für ein **FireBeetle ESP32**, das einen Bürsten-DC-Motor (**RS-540**) über einen Brushed-ESC (**THW-1060 / 1060-Klasse**) mit **RC-Servo-PWM** ansteuert.

Steuerung über:
- **USB-Serial** (Arduino Serial Monitor)
- **Bluetooth Low Energy UART** (Adafruit Bluefruit / Nordic UART kompatibel)

Ziel: **sicherer Motor/ESC Bring-Up** als Vorbereitung für ein späteres Balancing mit IMU-Regelung.

---

<a id="de-features"></a>
## 2. Features
- **Forward-Only Policy (Sicherheit):**  
  PWM-Pulse unterhalb der Forward-Schwelle werden auf **Neutral/Stop** geklemmt (kein Reverse).
- **ESC Arming bei Neutral:**  
  Beim Booten wird **~3 s Neutral** gehalten, danach Stop.
- **Zwei Steuerwege:** Serial + BLE UART
- **Soft-Start/Ramp:** `RAMP <ms>`
- **Status-Ausgabe:** `STATUS`
- **CSV Logging:** für Excel/Python

---

<a id="de-sicherheit"></a>
## 3. Sicherheitshinweise (zuerst lesen)
1. Motor/Reaktionsrad **mechanisch sichern** (Klemme/Gehäuse), Schutzbrille.
2. **Hochstromfähige** Kabel/Stecker verwenden.
3. Finger/Kabel von rotierenden Teilen fernhalten.
4. Langsam starten: `SET 5`, dann steigern.
5. Not-Stop: `STOP` oder Stromversorgung sicher trennen.

> RS-540 Motoren können bei Blockade sehr hohe Ströme ziehen.

---

<a id="de-hardware"></a>
## 4. Hardware
**Pflicht**
- FireBeetle ESP32
- THW-1060 (oder 1060-Klasse) Brushed ESC (Servo-PWM Eingang)
- RS-540PH-7021 Motor
- 2S LiPo (7.4–8.4 V)

**Optional**
- Reaktionsrad + Abdeckung
- Sicherung/Schalter
- Labornetzteil mit Strombegrenzung

---

<a id="de-verdrahtung"></a>
## 5. Verdrahtung

### 5.1 Minimum
- ESC **Signal** → ESP32 **GPIO 16**
- ESC **GND** → ESP32 **GND** (gemeinsame Masse ist Pflicht!)
- ESC Motorleitungen → Motor
- Akku → ESC (Polung beachten)

**ASCII**
```text
Akku -> ESC -> Motor
ESC Signal -> ESP32 GPIO16
ESC GND    -> ESP32 GND
```

### 5.2 Hinweis zur roten Leitung (BEC)
Manche ESCs liefern 5V (BEC) über das Servokabel (rot).  
Wenn ESP32 über USB versorgt wird, ist rot meist **nicht nötig**.

⚠️ Niemals 5V auf 3.3V-Pins geben.

---

<a id="de-software"></a>
## 6. Software Setup (Arduino IDE)
- ESP32 Boardpaket: **“ESP32 by Espressif Systems”**
- Library: **ESP32Servo** installieren

---

<a id="de-upload"></a>
## 7. Build & Upload

### 7.1 Sketch-Struktur
Alle `.ino` Dateien müssen im selben Sketch-Ordner liegen:

```text
Prototyping_ESC_UART/
  Prototyping_ESC_UART.ino
  functions.ino
```

Falls deine Dateien so heißen:
- `20251218_main_Prototyping.ino`
- `20251218_funktion_Prototyping.ino`

…beide in denselben Ordner legen und die Hauptdatei passend umbenennen.

### 7.2 Upload
1. ESP32 per USB verbinden
2. Tools → Port auswählen
3. Upload
4. Serial Monitor **115200** öffnen

---

<a id="de-bedienung"></a>
## 8. Bedienung

<a id="de-serial"></a>
### 8.1 Serielle Kommandos
Wichtige Kommandos:
- `HELP`, `STATUS`
- `STOP`
- `SET <0..100>`
- `RAMP <ms>`
- `LOG START|STOP`, `LOG PERIOD <ms>`
- `US <1000..2000>` (Debug, forward-only geklemmt)

Sicherer Ersttest:
`STOP` → `RAMP 500` → `SET 5` → langsam erhöhen.

---

<a id="de-ble"></a>
### 8.2 BLE UART (Handy-App)
- Gerätename: `FireBeetle_ESC_UART`
- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

Bluefruit LE Connect:
- verbinden → UART Tab → Kommandos senden

Control Pad (falls in deinem Code aktiv):
- Button 1: FULL
- Button 2: STOP
- Up/Down: ±5%

---

<a id="de-logging"></a>
### 8.3 Logging (CSV)
Typisches Format:
```text
t_ms,tag,connected,armed,targetPct,outputPct,pulse_us,rpm_est
```

Workflow:
1. `LOG PERIOD 100`
2. `LOG START`
3. Test fahren
4. `LOG STOP`
5. Output kopieren und in Excel/Python plotten

---

<a id="de-konfig"></a>
## 9. Konfiguration
- ESC Pin: **GPIO16**
- Neutral: **1500 µs**
- Forward start: **~1520 µs**
- Max forward: **2000 µs**
- Ramp: per `RAMP <ms>`
- Batteriespannung (Schätzung): `8.4 V`

---

<a id="de-fehlersuche"></a>
## 10. Fehlersuche
- Motor dreht nicht: Masse prüfen, ESC Arming/Calibration, `SET 5` testen
- Start erst bei hohem %: `usMinFwd` fein anpassen
- BLE nicht sichtbar: reboot, keine Doppelverbindung, Berechtigungen
- Sprünge zu schnell: `RAMP 800` oder höher

---

<a id="de-ausblick"></a>
## 11. Ausblick
- IMU (MPU6050) + Winkelschätzung
- Regelung (PID/LQR/Zustandsregler)
- Battery-ADC + Cutoff
- bessere Telemetrie + Logging

---

<a id="de-referenzen"></a>
## 12. Referenzen
- Adafruit / Nordic UART Service:  
  https://learn.adafruit.com/introducing-adafruit-ble-bluetooth-low-energy-friend/uart-service
- ESP32Servo:  
  https://docs.arduino.cc/libraries/esp32servo/
