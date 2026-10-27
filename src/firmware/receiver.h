#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace presserator::client {

// Feed one complete protocol packet from the transport (for example, a BLE
// GATT write). Repeated and out-of-order frames are rejected.
esp_err_t protocol_receive(const uint8_t* packet, size_t size);

// Call when the transport disconnects so no key remains held on the USB host.
void protocol_reset();

} // namespace presserator::client
