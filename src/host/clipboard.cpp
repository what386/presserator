#include "clipboard.h"

#include <gio/gio.h>

#include <cstdio>

namespace presserator::host {
namespace {

bool read_with(const char* const* command, std::string* text) {
    GError* error = nullptr;
    GSubprocess* process = g_subprocess_newv(
        command, static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                               G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error);
    if (process == nullptr) {
        std::fprintf(stderr, "start clipboard reader %s: %s\n", command[0], error->message);
        g_clear_error(&error);
        return false;
    }

    GBytes* output = nullptr;
    GBytes* diagnostics = nullptr;
    const bool communicated = g_subprocess_communicate(process, nullptr, nullptr,
                                                        &output, &diagnostics, &error);
    const bool success = communicated && g_subprocess_get_successful(process);
    if (success) {
        gsize length = 0;
        const auto* bytes = static_cast<const char*>(g_bytes_get_data(output, &length));
        if (length == 0) {
            text->clear();
        } else {
            text->assign(bytes, length);
        }
    } else if (error != nullptr) {
        std::fprintf(stderr, "read clipboard: %s\n", error->message);
    } else {
        gsize length = 0;
        const auto* bytes = static_cast<const char*>(g_bytes_get_data(diagnostics, &length));
        std::fprintf(stderr, "read clipboard with %s failed: %.*s\n", command[0],
                     static_cast<int>(length), length == 0 ? "" : bytes);
    }
    g_clear_error(&error);
    if (output != nullptr) {
        g_bytes_unref(output);
    }
    if (diagnostics != nullptr) {
        g_bytes_unref(diagnostics);
    }
    g_object_unref(process);
    return success;
}

} // namespace

bool read_clipboard(std::string* text) {
    const char* wayland[] = {"wl-paste", "--no-newline", "--type", "text", nullptr};
    const char* x11[] = {"xclip", "-selection", "clipboard", "-out", nullptr};
    if (g_getenv("WAYLAND_DISPLAY") != nullptr) {
        return read_with(wayland, text);
    }
    if (g_getenv("DISPLAY") != nullptr) {
        return read_with(x11, text);
    }
    std::fprintf(stderr, "No Wayland or X11 clipboard is available.\n");
    return false;
}

} // namespace presserator::host
