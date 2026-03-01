/**
 * touch_to_wake.ino — Touch-to-Wake for Waveshare ESP32-S3-Touch-LCD-4.3
 * 
 * Turns backlight off after a configurable inactivity timeout.
 * Wakes on touch via LVGL wake-probe (no I2C bus conflict).
 * 
 * Based on project_aura by 21CNCStudio (GPL-3.0):
 *   https://github.com/21cncstudio/project_aura
 * 
 * Hardware required:
 *   - Waveshare ESP32-S3-Touch-LCD-4.3
 *   - 470µF 10V electrolytic capacitor between 3V3 and GND
 *     (connect via Sensor/AD PH2.0 connector)
 * 
 * Software required:
 *   - Modified lvgl_v8_port.cpp/h (included) with wake-probe API
 *   - ESP32_Display_Panel library
 *   - LVGL v8.4.x
 * 
 * Arduino IDE settings:
 *   Board:  ESP32S3 Dev Module
 *   Flash:  8 MB
 *   PSRAM:  OPI PSRAM
 *   USB CDC: Enabled
 * 
 * How it works:
 *   1. Screen ON  → normal LVGL touch operation
 *   2. Timeout    → backlight OFF, touch enters "probe mode"
 *   3. Probe mode → touch sets a wake flag (no LVGL events generated)
 *   4. Main loop  → polls wake flag, turns backlight ON
 *   5. Post-wake  → touch blocked briefly to prevent accidental clicks
 */

#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <lvgl_v8_port.h>

using namespace esp_panel::board;
using namespace esp_panel::drivers;

// ── Configuration ───────────────────────────────────────────
static constexpr uint32_t SLEEP_TIMEOUT_MS = 30000;  // Backlight off after 30s idle
static constexpr uint32_t WAKE_BLOCK_MS    = 250;    // Block touch input after wake (ms)
static constexpr uint32_t BOOT_GRACE_MS    = 3000;   // Don't sleep right after boot

// ── State ───────────────────────────────────────────────────
static Board     *board             = nullptr;
static Backlight *panelBacklight    = nullptr;
static bool       backlightOn       = true;
static uint32_t   bootGraceUntilMs  = 0;
static uint32_t   blockInputUntilMs = 0;

// ── Backlight control ───────────────────────────────────────
static void setBacklight(bool on) {
    if (!panelBacklight || on == backlightOn) return;

    if (on) {
        panelBacklight->on();
    } else {
        panelBacklight->off();
    }

    backlightOn = on;
    lvgl_port_set_wake_touch_probe(!on);

    if (on) {
        lv_disp_trig_activity(NULL);
    }
}

static void consumePendingInput() {
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_reset(indev, NULL);
        }
        indev = lv_indev_get_next(indev);
    }
}

// ── Poll (call from loop) ───────────────────────────────────
static void backlightPoll() {
    uint32_t now = millis();

    if (bootGraceUntilMs != 0 && now < bootGraceUntilMs) return;
    bootGraceUntilMs = 0;

    // Screen OFF → check wake probe
    if (!backlightOn) {
        if (lvgl_port_take_wake_touch_pending()) {
            setBacklight(true);
            blockInputUntilMs = now + WAKE_BLOCK_MS;
            lvgl_port_block_touch_read(WAKE_BLOCK_MS);
            consumePendingInput();
        }
        return;
    }

    // Screen ON → consume blocked input, check timeout
    if (blockInputUntilMs != 0) {
        if (now < blockInputUntilMs) {
            consumePendingInput();
        } else {
            blockInputUntilMs = 0;
        }
    }

    lv_disp_t *disp = lv_disp_get_default();
    if (!disp) return;

    uint32_t inactiveMs = lv_disp_get_inactive_time(disp);
    if (SLEEP_TIMEOUT_MS > 0 && inactiveMs >= SLEEP_TIMEOUT_MS) {
        setBacklight(false);
    }
}

// ── Setup ───────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Touch-to-Wake Demo ===");

    // Board init
    board = new Board();
    board->init();
    board->begin();
    panelBacklight = board->getBacklight();

    // LVGL init (uses modified lvgl_v8_port with wake-probe support)
    // NOTE: If getLCD() doesn't compile, try getLcd() — depends on library version.
    lvgl_port_init(board->getLCD(), board->getTouch());

    // ── Your UI goes here ───────────────────────────────────
    lvgl_port_lock(-1);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x003366), 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Touch-to-Wake Demo\n\nScreen sleeps after 30s\nTouch to wake");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    lvgl_port_unlock();

    // Initial state
    lvgl_port_set_wake_touch_probe(false);
    bootGraceUntilMs = millis() + BOOT_GRACE_MS;
    backlightOn = true;

    Serial.println("[Init] Ready");
}

// ── Loop ────────────────────────────────────────────────────
void loop() {
    backlightPoll();
    delay(5);
}
