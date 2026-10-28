#include "clipboard.h"
#include "sender.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace {

struct KeyStroke {
    uint8_t usage;
    uint8_t modifiers;
};

constexpr uint8_t kLeftShift = 1u << 1;
constexpr uint8_t kLeftControl = 1u;
constexpr uint8_t kInteractiveQuit = 0x1C;

class TerminalInputMode {
public:
    bool enable() {
        if (!::isatty(STDIN_FILENO)) {
            return true;
        }
        if (::tcgetattr(STDIN_FILENO, &original_) < 0) {
            std::fprintf(stderr, "read terminal settings: %s\n", std::strerror(errno));
            return false;
        }

        termios interactive = original_;
        interactive.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG | IEXTEN));
        interactive.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL | INLCR | IGNCR));
        interactive.c_cc[VMIN] = 1;
        interactive.c_cc[VTIME] = 0;
        if (::tcsetattr(STDIN_FILENO, TCSANOW, &interactive) < 0) {
            std::fprintf(stderr, "set terminal input mode: %s\n", std::strerror(errno));
            return false;
        }
        interactive_ = true;
        terminal_output_ = ::isatty(STDOUT_FILENO) ? stdout :
                           (::isatty(STDERR_FILENO) ? stderr : nullptr);
        if (terminal_output_ != nullptr) {
            std::fputs("\x1b]1337;SetUserVar=presserator=MQ==\x07", terminal_output_);
            std::fputs("\x1b[?2004h", terminal_output_);
            std::fflush(terminal_output_);
        }
        return true;
    }

    ~TerminalInputMode() {
        if (interactive_) {
            if (terminal_output_ != nullptr) {
                std::fputs("\x1b]1337;SetUserVar=presserator=\x07", terminal_output_);
                std::fputs("\x1b[?2004l", terminal_output_);
                std::fflush(terminal_output_);
            }
            (void)::tcsetattr(STDIN_FILENO, TCSANOW, &original_);
        }
    }

    bool interactive() const { return interactive_; }

private:
    termios original_{};
    bool interactive_ = false;
    FILE* terminal_output_ = nullptr;
};

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
    case 0x04: key->usage = 0x07; key->modifiers = kLeftControl; return true;
    case '\b':
    case 0x7F: key->usage = 0x2A; key->modifiers = 0; return true;
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

class TextSender {
public:
    explicit TextSender(presserator::host::Sender& sender) : sender_(sender) {}

    bool send(uint8_t character) {
        if (character == '\n' && previous_was_cr_) {
            previous_was_cr_ = false;
            return true;
        }
        previous_was_cr_ = character == '\r';

        KeyStroke key{};
        if (!map_ascii(character, &key)) {
            std::fprintf(stderr,
                         "unsupported input byte 0x%02x; this version accepts ASCII only\n",
                         character);
            return false;
        }
        return sender_.send_key(key.usage, key.modifiers);
    }

private:
    presserator::host::Sender& sender_;
    bool previous_was_cr_ = false;
};

void print_usage(const char* program) {
    std::fprintf(stderr, "Usage: %s [--clipboard] [BLE-device-address] [< text.txt]\n",
                 program);
}

} // namespace

int main(int argc, char** argv) {
    bool clipboard_mode = false;
    const char* address = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--clipboard") == 0 && !clipboard_mode) {
            clipboard_mode = true;
        } else if (argv[i][0] != '-' && address == nullptr) {
            address = argv[i];
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    std::string clipboard_text;
    if (clipboard_mode) {
        if (!presserator::host::read_clipboard(&clipboard_text)) {
            return 1;
        }
        for (unsigned char character : clipboard_text) {
            KeyStroke key{};
            if (!map_ascii(character, &key)) {
                std::fprintf(stderr,
                             "unsupported clipboard byte 0x%02x; this version accepts ASCII only\n",
                             character);
                return 1;
            }
        }
        if (clipboard_text.empty()) {
            return 0;
        }
    }

    presserator::host::BleTransport transport;
    if (!transport.connect(address)) {
        return 1;
    }
    presserator::host::Sender sender(transport);
    TextSender text_sender(sender);

    if (clipboard_mode) {
        for (unsigned char character : clipboard_text) {
            if (!text_sender.send(character)) {
                return 1;
            }
        }
        return 0;
    }

    TerminalInputMode input_mode;
    if (!input_mode.enable()) {
        return 1;
    }
    if (input_mode.interactive()) {
        std::fprintf(stderr,
                     "Interactive mode: type or press Ctrl-Shift-V to send; Ctrl-Shift-C to quit.\n");
    }

    bool in_paste = false;
    size_t marker_chars = 0;
    const auto send_input = [&](uint8_t character, bool pasted) {
        if (input_mode.interactive() && !pasted && character == kInteractiveQuit) {
            return 2; // Quit shortcut sent by the terminal.
        }
        if (input_mode.interactive() && !pasted && character == 0x03) {
            return 130; // Ctrl-C
        }
        return text_sender.send(character) ? 0 : 1;
    };

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
            if (input_mode.interactive()) {
                const char* marker = in_paste ? "\x1b[201~" : "\x1b[200~";
                if (character == static_cast<uint8_t>(marker[marker_chars])) {
                    ++marker_chars;
                    if (marker[marker_chars] == '\0') {
                        in_paste = !in_paste;
                        marker_chars = 0;
                    }
                    continue;
                }
                if (marker_chars != 0) {
                    for (size_t j = 0; j < marker_chars; ++j) {
                        const int result = send_input(static_cast<uint8_t>(marker[j]), in_paste);
                        if (result != 0) {
                            return result == 2 ? 0 : result;
                        }
                    }
                    marker_chars = 0;
                    if (character == static_cast<uint8_t>(marker[0])) {
                        marker_chars = 1;
                        continue;
                    }
                }
            }
            const int result = send_input(character, in_paste);
            if (result != 0) {
                return result == 2 ? 0 : result;
            }
        }
    }

    return 0;
}
