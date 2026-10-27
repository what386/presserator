#pragma once

#include <cstdint>

#include "bluetooth.h"

namespace presserator::host {

class Sender {
public:
    explicit Sender(BleTransport& transport);

    // Sends one HID key-down report followed by a key-up report.
    bool send_key(uint8_t usage, uint8_t modifiers);

private:
    BleTransport& transport_;
    uint16_t sequence_ = 0;
};

} // namespace presserator::host
