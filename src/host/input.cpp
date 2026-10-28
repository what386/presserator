#include "cli.h"
#include "clipboard.h"
#include "sender.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <string>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace cli = presserator::host::cli;

namespace {

struct KeyStroke {
    uint8_t usage;
    uint8_t modifiers;
};

constexpr uint8_t kLeftShift = 1u << 1;
constexpr uint8_t kLeftControl = 1u;
constexpr uint8_t kInteractiveQuit = 0x1C;

termios g_original_terminal{};
volatile sig_atomic_t g_terminal_active = 0;
int g_terminal_output = -1;

void quit_immediately(int signal) {
    if (g_terminal_active) {
        (void)::tcsetattr(STDIN_FILENO, TCSANOW, &g_original_terminal);
        constexpr char reset[] = "\x1b[<u\x1b]1337;SetUserVar=presserator=\x07\x1b[?2004l";
        if (g_terminal_output >= 0) {
            (void)::write(g_terminal_output, reset, sizeof(reset) - 1);
        }
    }
    cli::interrupt();
    ::_exit(signal == SIGQUIT ? 0 : 130);
}

class TerminalInputMode {
public:
    bool enable() {
        if (!::isatty(STDIN_FILENO)) {
            return true;
        }
        if (::tcgetattr(STDIN_FILENO, &original_) < 0) {
            cli::error("read terminal settings: %s", std::strerror(errno));
            return false;
        }

        termios interactive = original_;
        interactive.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | IEXTEN));
        interactive.c_lflag |= ISIG;
        interactive.c_cc[VQUIT] = kInteractiveQuit;
        interactive.c_cc[VINTR] = _POSIX_VDISABLE;
        interactive.c_cc[VSUSP] = _POSIX_VDISABLE;
        interactive.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL | INLCR | IGNCR));
        interactive.c_cc[VMIN] = 1;
        interactive.c_cc[VTIME] = 0;
        if (::tcsetattr(STDIN_FILENO, TCSANOW, &interactive) < 0) {
            cli::error("set terminal input mode: %s", std::strerror(errno));
            return false;
        }
        interactive_ = true;
        terminal_output_ = ::isatty(STDOUT_FILENO) ? stdout :
                           (::isatty(STDERR_FILENO) ? stderr : nullptr);
        if (terminal_output_ != nullptr) {
            std::fputs("\x1b]1337;SetUserVar=presserator=MQ==\x07", terminal_output_);
            std::fputs("\x1b[?2004h", terminal_output_);
            std::fputs("\x1b[>1u", terminal_output_); // Enhanced keys, when supported.
            std::fflush(terminal_output_);
        }
        g_original_terminal = original_;
        g_terminal_output = terminal_output_ == nullptr ? -1 : ::fileno(terminal_output_);
        g_terminal_active = 1;
        struct sigaction action{};
        action.sa_handler = quit_immediately;
        ::sigemptyset(&action.sa_mask);
        (void)::sigaction(SIGQUIT, &action, &old_quit_);
        (void)::sigaction(SIGINT, &action, &old_interrupt_);
        return true;
    }

    ~TerminalInputMode() {
        if (interactive_) {
            if (terminal_output_ != nullptr) {
                std::fputs("\x1b[<u", terminal_output_);
                std::fputs("\x1b]1337;SetUserVar=presserator=\x07", terminal_output_);
                std::fputs("\x1b[?2004l", terminal_output_);
                std::fflush(terminal_output_);
            }
            (void)::tcsetattr(STDIN_FILENO, TCSANOW, &original_);
            g_terminal_active = 0;
            (void)::sigaction(SIGQUIT, &old_quit_, nullptr);
            (void)::sigaction(SIGINT, &old_interrupt_, nullptr);
        }
    }

    bool interactive() const { return interactive_; }

private:
    termios original_{};
    bool interactive_ = false;
    FILE* terminal_output_ = nullptr;
    struct sigaction old_quit_{};
    struct sigaction old_interrupt_{};
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
            cli::error(
                         "unsupported input byte 0x%02x; this version accepts ASCII only",
                         character);
            return false;
        }
        if (!sender_.send_key(key.usage, key.modifiers)) return false;
        cli::key(key.usage, key.modifiers);
        return true;
    }

private:
    presserator::host::Sender& sender_;
    bool previous_was_cr_ = false;
};

class InteractiveInput {
public:
    InteractiveInput(presserator::host::Sender& sender, TextSender& text)
        : sender_(sender), text_(text) {}

    bool pending() const { return !sequence_.empty() && !pasting_; }

    bool expire() {
        const bool escape = sequence_ == "\x1b";
        sequence_.clear();
        return !escape || send_key(0x29, 0);
    }

    bool feed(uint8_t byte) {
        if (pasting_) {
            constexpr char end[] = "\x1b[201~";
            if (byte == static_cast<uint8_t>(end[sequence_.size()])) {
                sequence_ += static_cast<char>(byte);
                if (sequence_.size() == sizeof(end) - 1) {
                    sequence_.clear();
                    pasting_ = false;
                }
                return true;
            }
            for (unsigned char pending : sequence_) {
                if (!text_.send(pending)) return false;
            }
            sequence_.clear();
            if (byte == 0x1b) { sequence_ = "\x1b"; return true; }
            KeyStroke key{};
            if (!map_ascii(byte, &key)) return unsupported();
            return text_.send(byte);
        }
        if (sequence_.empty()) {
            if (byte == 0x1b) { sequence_ = "\x1b"; return true; }
            return character(byte, 0);
        }
        if (sequence_ == "\x1b" && byte != '[' && byte != 'O') {
            sequence_.clear();
            return character(byte, 0x04); // Alt prefix.
        }
        sequence_ += static_cast<char>(byte);
        if (sequence_.size() == 2) return true;
        if (sequence_.size() > 64) { sequence_.clear(); return unsupported(); }
        if (byte < 0x40 || byte > 0x7e) return true;
        const std::string sequence = sequence_;
        sequence_.clear();
        return decode(sequence);
    }

private:
    bool send_key(uint8_t usage, uint8_t modifiers) {
        if (!sender_.send_key(usage, modifiers)) return false;
        cli::key(usage, modifiers);
        return true;
    }

    static bool unsupported() {
        cli::notice("Unsupported key ignored");
        return true;
    }

    bool character(unsigned code, uint8_t modifiers) {
        KeyStroke key{};
        if (code == 27) return send_key(0x29, modifiers);
        if (code == 0) return send_key(0x2c, modifiers | kLeftControl);
        if (code >= 1 && code <= 26 && code != 8 && code != 9 && code != 10 && code != 13) {
            return send_key(static_cast<uint8_t>(0x04 + code - 1), modifiers | kLeftControl);
        }
        if (code >= 28 && code <= 31) {
            constexpr uint8_t usages[] = {0x31, 0x30, 0x23, 0x2d};
            return send_key(usages[code - 28], modifiers | kLeftControl |
                                   (code >= 30 ? kLeftShift : 0));
        }
        if (code > 127 || !map_ascii(static_cast<uint8_t>(code), &key)) return unsupported();
        return send_key(key.usage, key.modifiers | modifiers);
    }

    bool decode(const std::string& sequence) {
        const char final = sequence.back();
        const std::string parameters = sequence.substr(2, sequence.size() - 3);
        unsigned values[4] = {};
        size_t count = 0;
        // Accept CSI-u event/alternate-key subparameters; releases need no report
        // because Sender emits a press followed by a release for every key.
        size_t pos = 0;
        while (pos < parameters.size() && count < 4) {
            unsigned value = 0;
            if (parameters[pos] < '0' || parameters[pos] > '9') return unsupported();
            while (pos < parameters.size() && parameters[pos] >= '0' && parameters[pos] <= '9') {
                value = value * 10 + static_cast<unsigned>(parameters[pos++] - '0');
                if (value > 1000000) return unsupported();
            }
            values[count++] = value;
            if (pos < parameters.size() && parameters[pos] == ':') {
                const size_t next = parameters.find(';', pos);
                if (final == 'u' && count == 2 && parameters.substr(pos, 2) == ":3") return true;
                pos = next == std::string::npos ? parameters.size() : next;
            }
            if (pos < parameters.size() && parameters[pos++] != ';') return unsupported();
        }
        if (pos != parameters.size()) return unsupported();
        const unsigned encoded = count >= 2 ? values[1] : 1;
        if (encoded < 1 || encoded > 16) return unsupported();
        const unsigned bits = encoded - 1;
        const uint8_t modifiers = ((bits & 1) ? kLeftShift : 0) |
                                  ((bits & 2) ? 0x04 : 0) |
                                  ((bits & 4) ? kLeftControl : 0) |
                                  ((bits & 8) ? 0x08 : 0);
        if (final == 'u' && count >= 1) return character(values[0], modifiers);
        if (final == '~' && count == 3 && values[0] == 27) return character(values[2], modifiers);
        if (final == '~' && count == 1 && values[0] == 200) { pasting_ = true; return true; }
        if (final == '~' && count >= 1) {
            uint8_t usage = 0;
            switch (values[0]) {
            case 1: case 7: usage = 0x4a; break;
            case 2: usage = 0x49; break;
            case 3: usage = 0x4c; break;
            case 4: case 8: usage = 0x4d; break;
            case 5: usage = 0x4b; break;
            case 6: usage = 0x4e; break;
            case 11: case 12: case 13: case 14: usage = 0x3a + values[0] - 11; break;
            case 15: usage = 0x3e; break;
            case 17: case 18: case 19: case 20: case 21: usage = 0x3f + values[0] - 17; break;
            case 23: case 24: usage = 0x44 + values[0] - 23; break;
            default: return unsupported();
            }
            return send_key(usage, modifiers);
        }
        uint8_t usage = 0;
        switch (final) {
        case 'A': usage = 0x52; break;
        case 'B': usage = 0x51; break;
        case 'C': usage = 0x4f; break;
        case 'D': usage = 0x50; break;
        case 'H': usage = 0x4a; break;
        case 'F': usage = 0x4d; break;
        case 'P': case 'Q': case 'R': case 'S': usage = 0x3a + final - 'P'; break;
        case 'Z': return send_key(0x2b, modifiers | kLeftShift);
        default: return unsupported();
        }
        return send_key(usage, modifiers);
    }

    presserator::host::Sender& sender_;
    TextSender& text_;
    std::string sequence_;
    bool pasting_ = false;
};

} // namespace

int main(int argc, char** argv) {
    cli::Session display;
    bool clipboard_mode = false;
    const char* address = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--clipboard") == 0 && !clipboard_mode) {
            clipboard_mode = true;
        } else if (argv[i][0] != '-' && address == nullptr) {
            address = argv[i];
        } else {
            cli::usage(argv[0]);
            return 2;
        }
    }

    TerminalInputMode input_mode;
    if (!input_mode.enable()) {
        return 1;
    }

    std::string clipboard_text;
    if (clipboard_mode) {
        if (!presserator::host::read_clipboard(&clipboard_text)) {
            return 1;
        }
        for (unsigned char character : clipboard_text) {
            KeyStroke key{};
            if (!map_ascii(character, &key)) {
                cli::error(
                             "unsupported clipboard byte 0x%02x; this version accepts ASCII only",
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
        cli::status("Sending clipboard");
        for (unsigned char character : clipboard_text) {
            if (!text_sender.send(character)) {
                return 1;
            }
        }
        cli::status(cli::Tone::Success, "Sent clipboard");
        return 0;
    }

    if (input_mode.interactive()) {
        cli::input_mode();
        cli::status(cli::Tone::Success, "Connected");
    }

    InteractiveInput keyboard(sender, text_sender);
    uint8_t buffer[256];
    while (true) {
        if (input_mode.interactive() && keyboard.pending()) {
            pollfd input{STDIN_FILENO, POLLIN, 0};
            const int ready = ::poll(&input, 1, 80);
            if (ready == 0) {
                if (!keyboard.expire()) return 1;
                continue;
            }
            if (ready < 0) {
                if (errno == EINTR) continue;
                cli::error("poll stdin: %s", std::strerror(errno));
                return 1;
            }
        }
        const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            cli::error("read stdin: %s", std::strerror(errno));
            return 1;
        }
        if (count == 0) {
            break;
        }

        for (ssize_t i = 0; i < count; ++i) {
            const uint8_t character = buffer[i];
            if (input_mode.interactive()) {
                if (!keyboard.feed(character)) return 1;
            } else if (!text_sender.send(character)) {
                return 1;
            }
        }
    }

    return 0;
}
