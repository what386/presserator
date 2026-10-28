#include "cli.h"

#include <cstdarg>
#include <cstdio>
#include <csignal>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/ioctl.h>
#include <unistd.h>

namespace presserator::host::cli {
namespace {

bool terminal = false;
volatile sig_atomic_t visible = 0;
std::string current_status;
Tone current_tone = Tone::Progress;
bool show_input = false;
std::string feedback;
std::string special_characters;
size_t edit_cursor = 0;
volatile sig_atomic_t cursor_row = 0;
volatile sig_atomic_t bottom_row = 0;

void erase_feedback(size_t position, size_t count) {
    feedback.erase(position, count);
    special_characters.erase(position, count);
}

void append(const std::string& text, bool special = false) {
    feedback.insert(edit_cursor, text);
    special_characters.insert(edit_cursor, text.size(), special ? 1 : 0);
    edit_cursor += text.size();
    if (feedback.size() > 65536) {
        const size_t remove = feedback.size() - 65536;
        erase_feedback(0, remove);
        edit_cursor = edit_cursor >= remove ? edit_cursor - remove : 0;
    }
}

const char* tone_color() {
    switch (current_tone) {
    case Tone::Success: return "\x1b[1;32m";
    case Tone::Warning: return "\x1b[1;33m";
    case Tone::Error: return "\x1b[1;31m";
    case Tone::Progress: return "\x1b[1;36m";
    }
    return "\x1b[0m";
}

void print_feedback(const std::string& text, const std::string& styles) {
    bool colored = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const bool special = styles[i] != 0;
        if (special != colored) std::fputs(special ? "\x1b[96m" : "\x1b[0m", stderr);
        colored = special;
        std::fputc(text[i], stderr);
    }
    std::fputs("\x1b[0m", stderr);
}

void erase_display() {
    if (!visible) return;
    std::fputc('\r', stderr);
    if (cursor_row) std::fprintf(stderr, "\x1b[%dA", static_cast<int>(cursor_row));
    std::fputs("\x1b[J", stderr);
    visible = 0;
}

void print_header(size_t columns) {
    size_t remaining = columns - 1;
    const auto segment = [&](std::string text, const char* color) {
        for (char& byte : text) {
            if (static_cast<unsigned char>(byte) < 0x20 || byte == 0x7f) byte = ' ';
        }
        if (text.size() > remaining) text.resize(remaining);
        remaining -= text.size();
        if (text.empty()) return;
        std::fprintf(stderr, "%s%s\x1b[0m", color, text.c_str());
    };
    segment("presserator", "\x1b[0m");
    segment(" | ", "\x1b[90m");
    segment(current_status, tone_color());
    if (show_input) {
        segment(" | ", "\x1b[90m");
        segment("Ctrl+Shift+V paste", "\x1b[0m");
        segment(" | ", "\x1b[90m");
        segment("Ctrl+Shift+C quit", "\x1b[0m");
    }
}

void redraw() {
    if (!terminal || current_status.empty()) return;
    winsize size{};
    (void)::ioctl(STDERR_FILENO, TIOCGWINSZ, &size);
    const size_t columns = size.ws_col ? size.ws_col : 80;
    const size_t height = size.ws_row ? size.ws_row : 24;
    const size_t prefix_width = columns > 3 ? 2 : 0;
    const size_t text_width = columns > prefix_width + 1 ? columns - prefix_width - 1 : 1;
    std::vector<std::string> lines;
    std::vector<std::string> styles;
    std::vector<bool> line_starts;
    std::string line;
    std::string style;
    bool logical_start = true;
    size_t caret_line = 0, caret_column = 0;
    for (size_t i = 0; i <= feedback.size(); ++i) {
        if (line.size() == text_width && i < feedback.size() && feedback[i] != '\n') {
            lines.push_back(line);
            styles.push_back(style);
            line_starts.push_back(logical_start);
            logical_start = false;
            line.clear();
            style.clear();
        }
        if (i == edit_cursor) {
            caret_line = lines.size();
            caret_column = line.size();
        }
        if (i == feedback.size()) break;
        if (feedback[i] == '\n') {
            lines.push_back(line);
            styles.push_back(style);
            line_starts.push_back(logical_start);
            logical_start = true;
            line.clear();
            style.clear();
        } else {
            line += feedback[i];
            style += special_characters[i];
        }
    }
    lines.push_back(line);
    styles.push_back(style);
    line_starts.push_back(logical_start);
    const size_t available_rows = height > 3 ? height - 3 : 1;
    const size_t first = caret_line >= available_rows ? caret_line - available_rows + 1 : 0;
    const size_t last = std::min(lines.size(), first + available_rows);

    erase_display();
    std::fputc('\r', stderr);
    print_header(columns);
    std::fprintf(stderr, "\r\n\x1b[90m%s\x1b[0m", std::string(columns - 1, '-').c_str());
    for (size_t i = first; i < last; ++i) {
        const char* prefix = prefix_width ? (line_starts[i] ? "> " : "  ") : "";
        std::fprintf(stderr, "\r\n%s", prefix);
        if (show_input) print_feedback(lines[i], styles[i]);
    }
    const size_t rows_below = last - caret_line - 1;
    if (rows_below) std::fprintf(stderr, "\x1b[%zuA", rows_below);
    std::fputc('\r', stderr);
    const size_t column = show_input ? prefix_width + caret_column : 0;
    if (column) std::fprintf(stderr, "\x1b[%zuC", column);
    cursor_row = static_cast<sig_atomic_t>(caret_line - first + 2);
    bottom_row = static_cast<sig_atomic_t>(last - first + 1);
    std::fflush(stderr);
    visible = 1;
}

void format_status(const char* format, va_list args) {
    char message[1024];
    std::vsnprintf(message, sizeof(message), format, args);
    current_status = message;
    redraw();
}

} // namespace

Session::Session() {
    terminal = ::isatty(STDERR_FILENO);
    current_status.clear();
    current_tone = Tone::Progress;
    feedback.clear();
    special_characters.clear();
    edit_cursor = 0;
    cursor_row = 0;
    bottom_row = 0;
    show_input = false;
    visible = 0;
}

Session::~Session() { interrupt(); }

void status(const char* format, ...) {
    current_tone = Tone::Progress;
    va_list args;
    va_start(args, format);
    format_status(format, args);
    va_end(args);
}

void status(Tone tone, const char* format, ...) {
    current_tone = tone;
    va_list args;
    va_start(args, format);
    format_status(format, args);
    va_end(args);
}

void error(const char* format, ...) {
    erase_display();
    if (terminal) std::fputs("\x1b[1;31m", stderr);
    std::fputs("presserator: ", stderr);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    if (terminal) std::fputs("\x1b[0m", stderr);
    std::fputc('\n', stderr);
    current_status = "Error";
    current_tone = Tone::Error;
    redraw();
}

void notice(const char* text) { status(Tone::Warning, "%s", text); }

void input_mode() {
    show_input = true;
    redraw();
}

void typed(uint8_t character) {
    if (!terminal || !show_input) return;
    switch (character) {
    case '\r': case '\n': append("\n"); break;
    case '\t': append("    "); break;
    case '\b': case 0x7f:
        if (edit_cursor > 0) erase_feedback(--edit_cursor, 1);
        break;
    default:
        if (character >= 0x20 && character < 0x7f) append(std::string(1, character));
        break;
    }
    redraw();
}

void key(uint8_t usage, uint8_t modifiers) {
    if (!terminal || !show_input) return;
    if (modifiers == 0) {
        switch (usage) {
        case 0x28: typed('\n'); return;
        case 0x2a: typed('\b'); return;
        case 0x2b: typed('\t'); return;
        case 0x4c:
            if (edit_cursor < feedback.size()) erase_feedback(edit_cursor, 1);
            redraw(); return;
        case 0x50: if (edit_cursor > 0) --edit_cursor; redraw(); return;
        case 0x4f: if (edit_cursor < feedback.size()) ++edit_cursor; redraw(); return;
        case 0x4a: {
            const size_t start = edit_cursor == 0 ? std::string::npos : feedback.rfind('\n', edit_cursor - 1);
            edit_cursor = start == std::string::npos ? 0 : start + 1;
            redraw(); return;
        }
        case 0x4d: {
            const size_t end = feedback.find('\n', edit_cursor);
            edit_cursor = end == std::string::npos ? feedback.size() : end;
            redraw(); return;
        }
        case 0x52: case 0x51: {
            const size_t preceding = edit_cursor == 0 ? std::string::npos : feedback.rfind('\n', edit_cursor - 1);
            const size_t start = preceding == std::string::npos ? 0 : preceding + 1;
            const size_t column = edit_cursor - start;
            if (usage == 0x52 && start > 0) {
                const size_t before = start < 2 ? std::string::npos : feedback.rfind('\n', start - 2);
                const size_t previous = before == std::string::npos ? 0 : before + 1;
                edit_cursor = previous + std::min(column, start - previous - 1);
            } else if (usage == 0x51) {
                const size_t end = feedback.find('\n', edit_cursor);
                if (end != std::string::npos) {
                    const size_t next_end = feedback.find('\n', end + 1);
                    edit_cursor = end + 1 + std::min(column,
                        (next_end == std::string::npos ? feedback.size() : next_end) - end - 1);
                }
            }
            redraw(); return;
        }
        default: break;
        }
    }
    const bool shifted = (modifiers & 0x22) != 0;
    std::string name;
    if (usage >= 4 && usage <= 29) name = char((shifted ? 'A' : 'a') + usage - 4);
    else if (usage >= 0x1e && usage <= 0x27) {
        name = (shifted ? "!@#$%^&*()" : "1234567890")[usage - 0x1e];
    } else if (usage >= 0x2d && usage <= 0x38 && usage != 0x32) {
        name = (shifted ? "_+{}|~:\"~<>?" : "-=[]\\#;'`,./")[usage - 0x2d];
    } else if (usage >= 0x3a && usage <= 0x45) name = "F" + std::to_string(usage - 0x3a + 1);
    else {
        switch (usage) {
        case 0x28: name = "Enter"; break;
        case 0x29: name = "Escape"; break;
        case 0x2a: name = "Backspace"; break;
        case 0x2b: name = "Tab"; break;
        case 0x2c: name = " "; break;
        case 0x49: name = "Insert"; break;
        case 0x4a: name = "Home"; break;
        case 0x4b: name = "PageUp"; break;
        case 0x4c: name = "Delete"; break;
        case 0x4d: name = "End"; break;
        case 0x4e: name = "PageDown"; break;
        case 0x4f: name = "Right"; break;
        case 0x50: name = "Left"; break;
        case 0x51: name = "Down"; break;
        case 0x52: name = "Up"; break;
        default: name = "Key"; break;
        }
    }
    if ((modifiers & ~0x22) == 0) {
        if (name.size() == 1) { typed(static_cast<uint8_t>(name[0])); return; }
        if (!shifted && usage == 0x2a) { typed('\b'); return; }
    }
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') name[0] -= 'a' - 'A';
    std::string label = "<";
    if (modifiers & 0x11) label += "Ctrl+";
    if (modifiers & 0x44) label += "Alt+";
    if (modifiers & 0x88) label += "Win+";
    if (shifted) label += "Shift+";
    label += name == " " ? "Space" : name;
    append(label + ">", true);
    redraw();
}

void usage(const char* program) {
    std::fprintf(stderr, "Usage: %s [--clipboard] [BLE-device-address] [< text.txt]\n", program);
}

void interrupt() {
    if (visible) {
        for (sig_atomic_t row = cursor_row; row < bottom_row; ++row) {
            (void)::write(STDERR_FILENO, "\x1b[1B", 4);
        }
        constexpr char reset[] = "\x1b[0m\r\n";
        (void)::write(STDERR_FILENO, reset, sizeof(reset) - 1);
        visible = 0;
    }
}

} // namespace presserator::host::cli
