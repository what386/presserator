#pragma once

#include <string>

namespace presserator::host {

// Reads the desktop clipboard as text, preserving its bytes and trailing newline.
bool read_clipboard(std::string* text);

} // namespace presserator::host
