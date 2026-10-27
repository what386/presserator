#pragma once

#include "protocol.h"

namespace presserator::host {

class BleTransport {
public:
    BleTransport();
    ~BleTransport();

    BleTransport(const BleTransport&) = delete;
    BleTransport& operator=(const BleTransport&) = delete;

    bool connect(const char* optional_address = nullptr);
    bool send(const protocol::Packet& packet);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace presserator::host
