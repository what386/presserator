#include "receiver.h"

#include "keyboard.h"
#include "protocol.h"

namespace presserator::client {
namespace {

bool g_have_sequence = false;
uint16_t g_last_sequence = 0;

} // namespace

esp_err_t protocol_receive(const uint8_t* packet, size_t size) {
    protocol::Frame frame{};
    if (!protocol::decode(packet, size, &frame)) {
        return ESP_ERR_INVALID_ARG;
    }

    bool session_start = frame.sequence == 0 && frame.state.modifiers == 0;
    for (uint8_t key : frame.state.keys) {
        session_start = session_start && key == 0;
    }
    if (g_have_sequence && !session_start) {
        const uint16_t advance = static_cast<uint16_t>(frame.sequence - g_last_sequence);
        if (advance == 0 || advance >= 0x8000) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    KeyboardInputReport report{};
    report.modifiers = frame.state.modifiers;
    for (size_t i = 0; i < 6; ++i) {
        report.keys[i] = frame.state.keys[i];
    }

    const esp_err_t err = keyboard_set_report(report);
    if (err == ESP_OK) {
        g_last_sequence = frame.sequence;
        g_have_sequence = true;
    }
    return err;
}

void protocol_reset() {
    g_have_sequence = false;
    g_last_sequence = 0;
    (void)keyboard_release_all();
}

} // namespace presserator::client
