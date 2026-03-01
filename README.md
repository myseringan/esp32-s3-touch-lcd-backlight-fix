# esp32-s3-touch-lcd-backlight-fix

Touch-to-wake with backlight sleep for the **Waveshare ESP32-S3-Touch-LCD-4.3**.

Turns the backlight off after a configurable inactivity timeout, and wakes it on touch — without crashes, brownouts, or I2C bus conflicts.

## The Problem

The Waveshare ESP32-S3-Touch-LCD-4.3 controls its backlight through a **CH422G I/O expander** which enables a **MP3302DJ boost converter**. Toggling the backlight via I2C causes two issues:

1. **Inrush current** — The MP3302 boost converter draws a large spike when enabled, causing the 3.3V rail to sag. This triggers brownout resets or watchdog crashes on the ESP32.

2. **I2C bus conflict** — The GT911 touch controller and CH422G share the same I2C bus (GPIO8/GPIO9). LVGL continuously polls touch via I2C. If the backlight is toggled (I2C write to CH422G) while a touch read is in progress, the bus collides — causing `i2c transaction failed` errors, screen tearing, or system crashes.

Waveshare has no official solution for this. Their support suggests deep-sleep GPIO wakeup, which is a completely different use case.

## The Solution

**Hardware: 470µF capacitor** absorbs the inrush current spike from the boost converter, preventing brownout/watchdog crashes.

**Software: LVGL wake-probe system** (adapted from [project_aura](https://github.com/21cncstudio/project_aura) by 21CNCStudio) replaces Wire-based touch polling with a probe mode built into the LVGL port layer:

- When the screen is **ON** → normal LVGL touch operation
- When the screen is **OFF** → touch enters "probe mode" which only sets a wake flag without generating LVGL events
- Main loop polls the wake flag and turns the backlight back on
- After wake, touch input is blocked for 250ms to prevent the wake-touch from accidentally clicking a UI button

No LVGL mutex lock is needed around the backlight toggle. No Wire library involvement. No I2C conflict.

## Hardware Setup

You need a **470µF 10V (or higher) electrolytic capacitor** connected between **3V3** and **GND**.

The easiest no-solder connection point is the **Sensor/AD PH2.0 connector** on the board:

```
Sensor/AD Connector (PH2.0, 3 pins):
  Pin 1: 3V3  ──→ Capacitor (+) long leg
  Pin 2: AD   ──→ leave disconnected
  Pin 3: GND  ──→ Capacitor (-) striped leg
```

Use the included PH2.0 cable and a screw terminal or breadboard — no soldering required.

> **⚠️ Polarity matters!** The stripe/minus side of the capacitor goes to GND. Reversed polarity can damage the capacitor.

## Files

| File | Description |
|------|-------------|
| `touch_to_wake.ino` | Main sketch with sleep/wake logic |
| `lvgl_v8_port.cpp` | Modified LVGL v8 port with wake-probe API |
| `lvgl_v8_port.h` | Header for the modified port |

## Installation

1. Install the required libraries in Arduino IDE:
   - [ESP32_Display_Panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
   - [LVGL v8.4.x](https://github.com/lvgl/lvgl)

2. Copy `lvgl_v8_port.cpp` and `lvgl_v8_port.h` into your project, **replacing** the default versions that come with ESP32_Display_Panel.

3. Open `touch_to_wake.ino` in Arduino IDE.

4. Set board configuration:
   - Board: **ESP32S3 Dev Module**
   - Flash Size: **8 MB**
   - PSRAM: **OPI PSRAM**
   - USB CDC On Boot: **Enabled**
   - Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**

5. Upload and connect the 470µF capacitor.

## Configuration

Edit the constants at the top of `touch_to_wake.ino`:

```cpp
static constexpr uint32_t SLEEP_TIMEOUT_MS = 30000;  // 30 seconds
static constexpr uint32_t WAKE_BLOCK_MS    = 250;    // Touch block after wake
static constexpr uint32_t BOOT_GRACE_MS    = 3000;   // Don't sleep right after boot
```

## API

The wake-probe system exposes three functions from `lvgl_v8_port.h`:

```cpp
// Enable/disable wake probe mode (call when backlight toggles)
bool lvgl_port_set_wake_touch_probe(bool enabled);

// Check and consume the wake-touch flag (call from loop)
bool lvgl_port_take_wake_touch_pending(void);

// Block touch reads for a duration (call after wake)
bool lvgl_port_block_touch_read(uint32_t duration_ms);
```

## Also Applies To

This fix should work for other Waveshare boards that use the same CH422G + MP3302 backlight architecture:
- ESP32-S3-Touch-LCD-4.3B
- ESP32-S3-Touch-LCD-7 (unverified)

## Root Cause Analysis

| Symptom | Cause | Fix |
|---------|-------|-----|
| BROWNOUT reset on `backlight->on()` | MP3302 inrush current sags 3.3V rail | 470µF capacitor |
| INT_WDT / TASK_WDT reset | I2C bus corrupted by voltage spike | Capacitor + software error recovery |
| `i2c transaction failed` | GT911 read + CH422G write collision | Wake-probe avoids concurrent I2C access |
| Wake touch clicks UI button | First touch triggers both wake + LVGL press | 250ms input block after wake |

## Credits

- **[project_aura](https://github.com/21cncstudio/project_aura)** by 21CNCStudio — the wake-probe approach and modified `lvgl_v8_port.cpp` are adapted from their open-source air quality monitor project (GPL-3.0).
- **[openHASP Discussion #602](https://github.com/HASwitchPlate/openHASP/discussions/602)** — community research on GPIO6 hardware mod alternative.

## License

GPL-3.0 (following project_aura's license for the derived lvgl_v8_port code).
