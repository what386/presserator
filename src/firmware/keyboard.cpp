#include "keyboard.h"

#include "class/hid/hid_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

namespace {

// HID boot protocol report: modifier byte, reserved byte, six key usages,
// and a five-bit LED output plus three bits of padding.
constexpr uint8_t kKeyboardReportDescriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       //   Usage Minimum (Left Control)
    0x29, 0xE7,       //   Usage Maximum (Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Variable, Absolute): modifiers
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Constant): reserved byte
    0x95, 0x05,       //   Report Count (5)
    0x75, 0x01,       //   Report Size (1)
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (Num Lock)
    0x29, 0x05,       //   Usage Maximum (Kana)
    0x91, 0x02,       //   Output (Data, Variable, Absolute): LEDs
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x03,       //   Report Size (3)
    0x91, 0x01,       //   Output (Constant): padding
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,       //   Usage Minimum (Reserved)
    0x29, 0x65,       //   Usage Maximum (Keyboard Application)
    0x81, 0x00,       //   Input (Data, Array, Absolute): key usages
    0xC0,             // End Collection
};

static_assert(sizeof(kKeyboardReportDescriptor) == presserator::kKeyboardReportDescriptorLength,
              "HID report descriptor length must match usb.cpp");

portMUX_TYPE g_reportMux = portMUX_INITIALIZER_UNLOCKED;
presserator::KeyboardInputReport g_currentReport{};
presserator::KeyboardLedReport g_ledReport = 0;

presserator::KeyboardInputReport current_report() {
    portENTER_CRITICAL(&g_reportMux);
    const auto report = g_currentReport;
    portEXIT_CRITICAL(&g_reportMux);
    return report;
}

esp_err_t send_current_report() {
    if (!tud_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    // BLE writes can arrive before the previous USB interrupt transfer finishes.
    // Wait for the endpoint so a key release cannot replace an unsent key press.
    const TickType_t started = xTaskGetTickCount();
    while (!tud_hid_ready()) {
        if (!tud_mounted() || xTaskGetTickCount() - started >= pdMS_TO_TICKS(100)) {
            return ESP_ERR_INVALID_STATE;
        }
        vTaskDelay(1);
    }

    const auto report = current_report();
    return tud_hid_keyboard_report(0, report.modifiers, report.keys)
               ? ESP_OK
               : ESP_ERR_INVALID_STATE;
}

} // namespace

extern "C" const uint8_t* tud_hid_descriptor_report_cb(uint8_t instance) {
    return instance == 0 ? kKeyboardReportDescriptor : nullptr;
}

namespace presserator {

void keyboard_init() {
    portENTER_CRITICAL(&g_reportMux);
    g_currentReport = {};
    g_ledReport = 0;
    portEXIT_CRITICAL(&g_reportMux);
}

esp_err_t keyboard_press(uint8_t key_usage) {
    const bool is_modifier = key_usage >= 0xE0 && key_usage <= 0xE7;
    if (!is_modifier && (key_usage < 0x04 || key_usage > 0x65)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!tud_hid_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&g_reportMux);
    if (is_modifier) {
        g_currentReport.modifiers |= static_cast<uint8_t>(1u << (key_usage - 0xE0));
        portEXIT_CRITICAL(&g_reportMux);
        return send_current_report();
    }

    size_t free_slot = sizeof(g_currentReport.keys);
    for (size_t i = 0; i < sizeof(g_currentReport.keys); ++i) {
        if (g_currentReport.keys[i] == key_usage) {
            portEXIT_CRITICAL(&g_reportMux);
            return send_current_report();
        }
        if (g_currentReport.keys[i] == 0 && free_slot == sizeof(g_currentReport.keys)) {
            free_slot = i;
        }
    }
    if (free_slot == sizeof(g_currentReport.keys)) {
        portEXIT_CRITICAL(&g_reportMux);
        return ESP_ERR_NO_MEM;
    }
    g_currentReport.keys[free_slot] = key_usage;
    portEXIT_CRITICAL(&g_reportMux);
    return send_current_report();
}

esp_err_t keyboard_release(uint8_t key_usage) {
    const bool is_modifier = key_usage >= 0xE0 && key_usage <= 0xE7;
    if (!is_modifier && (key_usage < 0x04 || key_usage > 0x65)) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_reportMux);
    if (is_modifier) {
        g_currentReport.modifiers &= static_cast<uint8_t>(~(1u << (key_usage - 0xE0)));
        portEXIT_CRITICAL(&g_reportMux);
        return send_current_report();
    }

    for (size_t i = 0; i < sizeof(g_currentReport.keys); ++i) {
        if (g_currentReport.keys[i] == key_usage) {
            g_currentReport.keys[i] = 0;
            break;
        }
    }
    portEXIT_CRITICAL(&g_reportMux);
    return send_current_report();
}

esp_err_t keyboard_set_report(const KeyboardInputReport& report) {
    for (uint8_t key : report.keys) {
        if (key != 0 && key != 0x01 && (key < 0x04 || key > 0x65)) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    portENTER_CRITICAL(&g_reportMux);
    g_currentReport = report;
    portEXIT_CRITICAL(&g_reportMux);
    return send_current_report();
}

esp_err_t keyboard_release_all() {
    portENTER_CRITICAL(&g_reportMux);
    g_currentReport = {};
    portEXIT_CRITICAL(&g_reportMux);
    return send_current_report();
}

KeyboardLedReport keyboard_led_state() {
    portENTER_CRITICAL(&g_reportMux);
    const auto leds = g_ledReport;
    portEXIT_CRITICAL(&g_reportMux);
    return leds;
}

} // namespace presserator

extern "C" uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                            hid_report_type_t report_type,
                                            uint8_t* buffer, uint16_t requested_length) {
    if (instance != 0 || report_id != 0 || buffer == nullptr) {
        return 0;
    }

    if (report_type == HID_REPORT_TYPE_INPUT) {
        const auto report = current_report();
        const size_t length = requested_length < sizeof(report) ? requested_length : sizeof(report);
        const auto* bytes = reinterpret_cast<const uint8_t*>(&report);
        for (size_t i = 0; i < length; ++i) {
            buffer[i] = bytes[i];
        }
        return static_cast<uint16_t>(length);
    }

    if (report_type == HID_REPORT_TYPE_OUTPUT && requested_length > 0) {
        buffer[0] = presserator::keyboard_led_state();
        return 1;
    }

    return 0;
}

extern "C" void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                                       hid_report_type_t report_type,
                                       const uint8_t* buffer, uint16_t buffer_size) {
    if (instance != 0 || report_id != 0 || report_type != HID_REPORT_TYPE_OUTPUT ||
        buffer == nullptr || buffer_size == 0) {
        return;
    }

    portENTER_CRITICAL(&g_reportMux);
    g_ledReport = buffer[0] & 0x1F;
    portEXIT_CRITICAL(&g_reportMux);
}
