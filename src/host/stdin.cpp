#include "sender.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace {

struct KeyStroke {
    uint8_t usage;
    uint8_t modifiers;
};

constexpr uint8_t kLeftShift = 1u << 1;

bool map_ascii(uint8_t character, KeyStroke* key) {
    if (character >= 'a' && character <= 'z') {
        key->usage = static_cast<uint8_t>(0x04 + character - 'a');
        key->modifiers = 0;
        return true;
    }
    if (character >= 'A' && character <= 'Z') {
        key->usage = static_cast<uint8_t>(0x04 + character - 'A');
        key->modifiers = kLeftShift;
        return true;
    }
    if (character >= '1' && character <= '9') {
        key->usage = static_cast<uint8_t>(0x1E + character - '1');
        key->modifiers = 0;
        return true;
    }
    if (character == '0') {
        key->usage = 0x27;
        key->modifiers = 0;
        return true;
    }

    switch (character) {
    case ' ': key->usage = 0x2C; key->modifiers = 0; return true;
    case '\n':
    case '\r': key->usage = 0x28; key->modifiers = 0; return true;
    case '\t': key->usage = 0x2B; key->modifiers = 0; return true;
    case '\b': key->usage = 0x2A; key->modifiers = 0; return true;
    case '-': key->usage = 0x2D; key->modifiers = 0; return true;
    case '_': key->usage = 0x2D; key->modifiers = kLeftShift; return true;
    case '=': key->usage = 0x2E; key->modifiers = 0; return true;
    case '+': key->usage = 0x2E; key->modifiers = kLeftShift; return true;
    case '[': key->usage = 0x2F; key->modifiers = 0; return true;
    case '{': key->usage = 0x2F; key->modifiers = kLeftShift; return true;
    case ']': key->usage = 0x30; key->modifiers = 0; return true;
    case '}': key->usage = 0x30; key->modifiers = kLeftShift; return true;
    case '\\': key->usage = 0x31; key->modifiers = 0; return true;
    case '|': key->usage = 0x31; key->modifiers = kLeftShift; return true;
    case ';': key->usage = 0x33; key->modifiers = 0; return true;
    case ':': key->usage = 0x33; key->modifiers = kLeftShift; return true;
    case '\'': key->usage = 0x34; key->modifiers = 0; return true;
    case '"': key->usage = 0x34; key->modifiers = kLeftShift; return true;
    case '`': key->usage = 0x35; key->modifiers = 0; return true;
    case '~': key->usage = 0x35; key->modifiers = kLeftShift; return true;
    case ',': key->usage = 0x36; key->modifiers = 0; return true;
    case '<': key->usage = 0x36; key->modifiers = kLeftShift; return true;
    case '.': key->usage = 0x37; key->modifiers = 0; return true;
    case '>': key->usage = 0x37; key->modifiers = kLeftShift; return true;
    case '/': key->usage = 0x38; key->modifiers = 0; return true;
    case '?': key->usage = 0x38; key->modifiers = kLeftShift; return true;
    case '!': key->usage = 0x1E; key->modifiers = kLeftShift; return true;
    case '@': key->usage = 0x1F; key->modifiers = kLeftShift; return true;
    case '#': key->usage = 0x20; key->modifiers = kLeftShift; return true;
    case '$': key->usage = 0x21; key->modifiers = kLeftShift; return true;
    case '%': key->usage = 0x22; key->modifiers = kLeftShift; return true;
    case '^': key->usage = 0x23; key->modifiers = kLeftShift; return true;
    case '&': key->usage = 0x24; key->modifiers = kLeftShift; return true;
    case '*': key->usage = 0x25; key->modifiers = kLeftShift; return true;
    case '(': key->usage = 0x26; key->modifiers = kLeftShift; return true;
    case ')': key->usage = 0x27; key->modifiers = kLeftShift; return true;
    default: return false;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::fprintf(stderr, "Usage: %s [BLE-device-address] < text.txt\n", argv[0]);
        return 2;
    }

    presserator::host::BleTransport transport;
    if (!transport.connect(argc == 2 ? argv[1] : nullptr)) {
        return 1;
    }
    presserator::host::Sender sender(transport);

    bool previous_was_cr = false;
    uint8_t buffer[256];
    while (true) {
        const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::fprintf(stderr, "read stdin: %s\n", std::strerror(errno));
            return 1;
        }
        if (count == 0) {
            break;
        }

        for (ssize_t i = 0; i < count; ++i) {
            const uint8_t character = buffer[i];
            if (character == '\n' && previous_was_cr) {
                previous_was_cr = false;
                continue;
            }
            previous_was_cr = character == '\r';

            KeyStroke key{};
            if (!map_ascii(character, &key)) {
                std::fprintf(stderr,
                             "unsupported input byte 0x%02x; this version accepts ASCII only\n",
                             character);
                return 1;
            }
            if (!sender.send_key(key.usage, key.modifiers)) {
                return 1;
            }
        }
    }

    return 0;
}
