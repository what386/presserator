#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace presserator::protocol {

inline constexpr size_t kPacketSize = 13;
inline constexpr uint8_t kProtocolVersion = 1;

struct KeyState {
    uint8_t modifiers = 0;
    uint8_t keys[6]{};
};

using Packet = std::array<uint8_t, kPacketSize>;

struct Frame {
    uint16_t sequence = 0;
    KeyState state{};
};

// Wire format: 'P','R', version, sequence LE, modifiers, six key usages, CRC-8.
Packet encode(uint16_t sequence, const KeyState& state);
bool decode(const uint8_t* bytes, size_t size, Frame* frame);

} // namespace presserator::protocol
