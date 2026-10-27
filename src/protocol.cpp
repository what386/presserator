#include "protocol.h"

namespace presserator::protocol {
namespace {

constexpr uint8_t kMagic0 = 'P';
constexpr uint8_t kMagic1 = 'R';
constexpr size_t kPayloadStart = 5;
constexpr size_t kCrcIndex = kPacketSize - 1;

uint8_t crc8(const uint8_t* data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) != 0 ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                                    : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

bool valid_key_usage(uint8_t usage) {
    // 0 is an unused slot; 1 is HID ErrorRollOver for >6 simultaneous keys.
    return usage == 0 || usage == 1 || (usage >= 0x04 && usage <= 0x65);
}

} // namespace

Packet encode(uint16_t sequence, const KeyState& state) {
    Packet packet{};
    packet[0] = kMagic0;
    packet[1] = kMagic1;
    packet[2] = kProtocolVersion;
    packet[3] = static_cast<uint8_t>(sequence & 0xFF);
    packet[4] = static_cast<uint8_t>(sequence >> 8);
    packet[5] = state.modifiers;
    for (size_t i = 0; i < 6; ++i) {
        packet[kPayloadStart + 1 + i] = state.keys[i];
    }
    packet[kCrcIndex] = crc8(packet.data(), kCrcIndex);
    return packet;
}

bool decode(const uint8_t* bytes, size_t size, Frame* frame) {
    if (bytes == nullptr || frame == nullptr || size != kPacketSize ||
        bytes[0] != kMagic0 || bytes[1] != kMagic1 || bytes[2] != kProtocolVersion ||
        bytes[kCrcIndex] != crc8(bytes, kCrcIndex)) {
        return false;
    }

    KeyState decoded{};
    decoded.modifiers = bytes[5];
    for (size_t i = 0; i < 6; ++i) {
        decoded.keys[i] = bytes[6 + i];
        if (!valid_key_usage(decoded.keys[i])) {
            return false;
        }
    }

    frame->sequence = static_cast<uint16_t>(bytes[3]) |
                      static_cast<uint16_t>(static_cast<uint16_t>(bytes[4]) << 8);
    frame->state = decoded;
    return true;
}

} // namespace presserator::protocol
