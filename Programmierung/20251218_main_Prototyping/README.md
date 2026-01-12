# FireBeetle ESP32 + THW-1060 + RS-540PH-7021 (Forward-only) with BLE + Serial Debug

## Goal
Spin a heavy reaction wheel as fast as possible using:
- FireBeetle ESP32
- THW-1060 brushed ESC
- RS-540PH-7021 motor
- 2S LiPo
- Control via Adafruit Bluefruit LE Connect (UART + Control Pad) and USB Serial

Motor reference (RS-540PH-7021 datasheet):
- ~20000 rpm @ 7.2 V no-load
- stall current ~50 A
=> Full throttle under load can draw very high current, heat the motor/ESC, and stress the battery.

ESC control reference:
- Standard servo pulses at 50 Hz, 1000..2000 us, neutral ~1500 us.

## Wiring (minimum)
1. Battery -> ESC power input
2. ESC motor outputs -> RS-540 motor
3. ESC signal wire -> ESP32 GPIO16 (PIN_ESC_SIGNAL in code)
4. ESC GND -> ESP32 GND  (IMPORTANT: common ground)

**Do NOT** connect the ESC BEC output to the ESP32 3.3V pin.
If you want to power the ESP32 from the ESC/BEC, you must use a proper 5V/VIN input path and verify your board supports it safely.
Simplest: power ESP32 via USB and only share GND with ESC.

## App / BLE
Use Adafruit "Bluefruit LE Connect":
- Connect to "FireBeetle_ESC_UART"
- UART tab: send commands as text
- Controller -> Control Pad:
  - Up/Down adjusts throttle
  - Button 1 = FULL
  - Button 2 = STOP

## Commands
- HELP
- ARM                 (neutral 3s, then ready)
- STOP                (neutral)
- FULL                (100% forward)
- SET <0..100>        (target throttle percent)
- RPM <value>         (target RPM estimate, based on KV and assumed battery voltage)
- US <1000..2000>     (debug: direct pulse width; still forward-only clamped)
- RAMP <ms>           (0 = immediate, default 300ms soft-start)
- DBG ON | DBG OFF
- LOG START | LOG STOP
- LOG PERIOD <ms>
- STATUS
- LED ON | LED OFF

## Logging
`LOG START` prints CSV lines:
t_ms,tag,connected,armed,targetPct,outputPct,pulse_us,rpm_est

Example:
12345,LOG,1,1,100,82.50,1910,19234

## Safety checklist
- Fix the reaction wheel mechanically (it can accelerate suddenly).
- Use thick wires and a battery with sufficient C rating.
- Avoid long stall conditions (stall current is very high).
- Add cooling if running at high power for long time.
