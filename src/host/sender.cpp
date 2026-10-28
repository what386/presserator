#include "sender.h"

#include "protocol.h"

namespace presserator::host {

Sender::Sender(BleTransport& transport) : transport_(transport) {}

bool Sender::send_key(uint8_t usage, uint8_t modifiers) {
    // A released state at sequence zero starts a host session on an existing link.
    if (sequence_ == 0 && !transport_.send(protocol::encode(sequence_++, {}))) {
        return false;
    }
    protocol::KeyState pressed{};
    pressed.modifiers = modifiers;
    pressed.keys[0] = usage;

    if (!transport_.send(protocol::encode(sequence_++, pressed))) {
        return false;
    }

    // Every text character is a press followed by a release report.
    return transport_.send(protocol::encode(sequence_++, {}));
}

} // namespace presserator::host
