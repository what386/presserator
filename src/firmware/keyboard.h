#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace presserator {

// USB HID boot-keyboard input report: modifiers, reserved byte, six usages.
struct KeyboardInputReport {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
};

// HID LED output bits: Num Lock, Caps Lock, Scroll Lock, Compose, Kana.
using KeyboardLedReport = uint8_t;

inline constexpr size_t kKeyboardInputReportSize = sizeof(KeyboardInputReport);
inline constexpr uint8_t kKeyboardReportDescriptorLength = 63;

// key_usage values are USB HID Keyboard/Keypad usage IDs (0x04 is 'A').
void keyboard_init();
esp_err_t keyboard_press(uint8_t key_usage);
esp_err_t keyboard_release(uint8_t key_usage);
esp_err_t keyboard_set_report(const KeyboardInputReport& report);
esp_err_t keyboard_release_all();
KeyboardLedReport keyboard_led_state();

} // namespace presserator

static_assert(presserator::kKeyboardInputReportSize == 8,
              "Boot keyboard input reports must be eight bytes");

extern "C" const uint8_t* tud_hid_descriptor_report_cb(uint8_t instance);
