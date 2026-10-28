#pragma once

#include <cstdint>

namespace presserator::host::cli {

enum class Tone { Progress, Success, Warning, Error };

// Owns the status line for one invocation. Redirected output has no status UI.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};

void status(const char* format, ...) __attribute__((format(printf, 1, 2)));
void status(Tone tone, const char* format, ...) __attribute__((format(printf, 2, 3)));
void error(const char* format, ...) __attribute__((format(printf, 1, 2)));
void notice(const char* text);
void usage(const char* program);
void input_mode();
void typed(uint8_t character);
void key(uint8_t usage, uint8_t modifiers);

// Async-signal-safe: leave the shell cursor on a fresh line on immediate quit.
void interrupt();

} // namespace presserator::host::cli
