#include "bluetooth.h"

#include <gio/gio.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

namespace presserator::host {
namespace {

constexpr char kServiceUuid[] = "b7e6a000-8f0d-4c2e-9f53-9e6c32e70d10";
constexpr char kCharacteristicUuid[] = "b7e6a001-8f0d-4c2e-9f53-9e6c32e70d10";
constexpr char kBluezName[] = "org.bluez";
constexpr char kObjectManagerPath[] = "/";
constexpr char kObjectManagerInterface[] = "org.freedesktop.DBus.ObjectManager";

void print_error(const char* action, GError* error) {
    std::fprintf(stderr, "%s: %s\n", action,
                 error != nullptr ? error->message : "unknown D-Bus error");
}

GVariant* get_objects(GDBusConnection* bus, GError** error) {
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kBluezName, kObjectManagerPath, kObjectManagerInterface,
        "GetManagedObjects", nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE, 15000, nullptr, error);
    if (reply == nullptr) {
        return nullptr;
    }
    GVariant* objects = nullptr;
    g_variant_get(reply, "(@a{oa{sa{sv}}})", &objects);
    g_variant_unref(reply);
    return objects;
}

GVariant* get_properties(GVariant* interfaces, const char* interface_name) {
    return g_variant_lookup_value(interfaces, interface_name, G_VARIANT_TYPE("a{sv}"));
}

bool has_uuid(GVariant* properties, const char* property_name, const char* wanted) {
    GVariant* values = g_variant_lookup_value(properties, property_name, G_VARIANT_TYPE("as"));
    if (values == nullptr) {
        return false;
    }
    bool found = false;
    GVariantIter iter;
    g_variant_iter_init(&iter, values);
    const char* value = nullptr;
    while (g_variant_iter_next(&iter, "&s", &value)) {
        if (g_ascii_strcasecmp(value, wanted) == 0) {
            found = true;
            break;
        }
    }
    g_variant_unref(values);
    return found;
}

bool uuid_equals(GVariant* properties, const char* wanted) {
    const gchar* uuid = nullptr;
    return g_variant_lookup(properties, "UUID", "&s", &uuid) &&
           g_ascii_strcasecmp(uuid, wanted) == 0;
}

std::string find_adapter(GVariant* objects) {
    GVariantIter iter;
    g_variant_iter_init(&iter, objects);
    const char* path = nullptr;
    GVariant* interfaces = nullptr;
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        GVariant* adapter = get_properties(interfaces, "org.bluez.Adapter1");
        g_variant_unref(interfaces);
        if (adapter != nullptr) {
            g_variant_unref(adapter);
            return path;
        }
    }
    return {};
}

std::string find_device(GVariant* objects, const char* requested_address) {
    GVariantIter iter;
    g_variant_iter_init(&iter, objects);
    const char* path = nullptr;
    GVariant* interfaces = nullptr;
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        GVariant* properties = get_properties(interfaces, "org.bluez.Device1");
        g_variant_unref(interfaces);
        if (properties == nullptr) {
            continue;
        }

        bool match = has_uuid(properties, "UUIDs", kServiceUuid);
        if (requested_address != nullptr) {
            const gchar* address = nullptr;
            if (g_variant_lookup(properties, "Address", "&s", &address) &&
                g_ascii_strcasecmp(address, requested_address) == 0) {
                match = true;
            } else {
                match = false;
            }
        }

        const std::string result = match ? path : "";
        g_variant_unref(properties);
        if (match) {
            return result;
        }
    }
    return {};
}

struct DeviceState {
    bool connected = false;
    bool services_resolved = false;
};

DeviceState device_state(GVariant* objects, const std::string& device_path) {
    GVariantIter iter;
    g_variant_iter_init(&iter, objects);
    const char* path = nullptr;
    GVariant* interfaces = nullptr;
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        if (device_path != path) {
            g_variant_unref(interfaces);
            continue;
        }
        GVariant* properties = get_properties(interfaces, "org.bluez.Device1");
        g_variant_unref(interfaces);
        DeviceState state;
        if (properties != nullptr) {
            gboolean connected = false;
            gboolean services_resolved = false;
            g_variant_lookup(properties, "Connected", "b", &connected);
            g_variant_lookup(properties, "ServicesResolved", "b", &services_resolved);
            state = {connected != 0, services_resolved != 0};
            g_variant_unref(properties);
        }
        return state;
    }
    return {};
}

std::string find_characteristic(GVariant* objects, const std::string& device_path) {
    std::string service_path;
    GVariantIter iter;
    g_variant_iter_init(&iter, objects);
    const char* path = nullptr;
    GVariant* interfaces = nullptr;
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        GVariant* properties = get_properties(interfaces, "org.bluez.GattService1");
        g_variant_unref(interfaces);
        if (properties != nullptr) {
            const gchar* device = nullptr;
            const bool match = uuid_equals(properties, kServiceUuid) &&
                               g_variant_lookup(properties, "Device", "&o", &device) &&
                               device_path == device;
            if (match) {
                service_path = path;
                g_variant_unref(properties);
                break;
            }
            g_variant_unref(properties);
        }
    }
    if (service_path.empty()) {
        return {};
    }

    g_variant_iter_init(&iter, objects);
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        GVariant* properties = get_properties(interfaces, "org.bluez.GattCharacteristic1");
        g_variant_unref(interfaces);
        if (properties != nullptr) {
            const gchar* service = nullptr;
            const bool match = uuid_equals(properties, kCharacteristicUuid) &&
                               g_variant_lookup(properties, "Service", "&o", &service) &&
                               service_path == service;
            g_variant_unref(properties);
            if (match) {
                return path;
            }
        }
    }
    return {};
}

bool call_method(GDBusConnection* bus, const char* path, const char* interface_name,
                 const char* method, GVariant* parameters, GError** error,
                 int timeout_ms = 15000) {
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kBluezName, path, interface_name, method, parameters,
        G_VARIANT_TYPE("()"), G_DBUS_CALL_FLAGS_NONE, timeout_ms, nullptr, error);
    if (reply == nullptr) {
        return false;
    }
    g_variant_unref(reply);
    return true;
}

} // namespace

struct BleTransport::Impl {
    GDBusConnection* bus = nullptr;
    std::string adapter_path;
    std::string device_path;
    std::string characteristic_path;
    bool discovery_started = false;

    ~Impl() {
        if (bus != nullptr) {
            g_object_unref(bus);
        }
    }
};

BleTransport::BleTransport() : impl_(new Impl) {}
BleTransport::~BleTransport() { delete impl_; }

bool BleTransport::connect(const char* optional_address) {
    GError* error = nullptr;
    impl_->bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (impl_->bus == nullptr) {
        print_error("connect to the system D-Bus", error);
        g_clear_error(&error);
        return false;
    }

    GVariant* objects = get_objects(impl_->bus, &error);
    if (objects == nullptr) {
        print_error("query BlueZ", error);
        g_clear_error(&error);
        return false;
    }
    impl_->adapter_path = find_adapter(objects);
    impl_->device_path = find_device(objects, optional_address);
    g_variant_unref(objects);
    if (impl_->adapter_path.empty()) {
        std::fprintf(stderr, "No BlueZ Bluetooth adapter was found.\n");
        return false;
    }

    if (impl_->device_path.empty()) {
        if (!call_method(impl_->bus, impl_->adapter_path.c_str(), "org.bluez.Adapter1",
                         "StartDiscovery", nullptr, &error)) {
            // BlueZ reports InProgress when another program is already scanning.
            g_clear_error(&error);
        } else {
            impl_->discovery_started = true;
        }
    }

    std::fprintf(stderr, "Searching for Presserator over Bluetooth LE...\n");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (impl_->device_path.empty() && std::chrono::steady_clock::now() < deadline) {
        objects = get_objects(impl_->bus, &error);
        if (objects == nullptr) {
            print_error("query BlueZ devices", error);
            g_clear_error(&error);
            return false;
        }
        impl_->device_path = find_device(objects, optional_address);
        if (!impl_->device_path.empty()) {
            g_variant_unref(objects);
            break;
        }
        g_variant_unref(objects);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    if (impl_->device_path.empty()) {
        std::fprintf(stderr, "No Presserator BLE device found. Check that it is powered and advertising.\n");
        return false;
    }
    std::fprintf(stderr, "Found Presserator at %s\n", impl_->device_path.c_str());

    objects = get_objects(impl_->bus, &error);
    if (objects == nullptr) {
        print_error("query BlueZ connection state", error);
        g_clear_error(&error);
        return false;
    }
    const bool already_connected = device_state(objects, impl_->device_path).connected;
    g_variant_unref(objects);

    // Reuse an existing link; only establish one when disconnected.
    bool connected = already_connected;
    for (int attempt = 0; !connected && attempt < 3; ++attempt) {
        if (call_method(impl_->bus, impl_->device_path.c_str(), "org.bluez.Device1",
                        "Connect", nullptr, &error, 30000)) {
            connected = true;
            break;
        }
        gchar* remote_error = g_dbus_error_get_remote_error(error);
        const bool already_connected = remote_error != nullptr &&
                                       std::strcmp(remote_error, "org.bluez.Error.AlreadyConnected") == 0;
        const bool aborted = error != nullptr &&
                             std::strstr(error->message, "le-connection-abort-by-local") != nullptr;
        g_free(remote_error);
        if (already_connected) {
            g_clear_error(&error);
            connected = true;
            break;
        }
        if (!aborted || attempt == 2) {
            print_error("connect to Presserator", error);
            g_clear_error(&error);
            return false;
        }
        g_clear_error(&error);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (!connected) {
        return false;
    }

    const auto services_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < services_deadline) {
        objects = get_objects(impl_->bus, &error);
        if (objects == nullptr) {
            print_error("discover Presserator GATT service", error);
            g_clear_error(&error);
            return false;
        }
        const DeviceState state = device_state(objects, impl_->device_path);
        if (state.connected && state.services_resolved) {
            impl_->characteristic_path = find_characteristic(objects, impl_->device_path);
        }
        g_variant_unref(objects);
        if (!impl_->characteristic_path.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    if (impl_->characteristic_path.empty()) {
        std::fprintf(stderr, "Presserator did not establish a ready GATT connection.\n");
        return false;
    }

    if (impl_->discovery_started) {
        call_method(impl_->bus, impl_->adapter_path.c_str(), "org.bluez.Adapter1",
                    "StopDiscovery", nullptr, &error);
        g_clear_error(&error);
        impl_->discovery_started = false;
    }
    std::fprintf(stderr, "Connected; sending text as keyboard input.\n");
    return true;
}

bool BleTransport::send(const protocol::Packet& packet) {
    if (impl_->bus == nullptr || impl_->characteristic_path.empty()) {
        std::fprintf(stderr, "Bluetooth transport is not connected.\n");
        return false;
    }

    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&options, "{sv}", "type", g_variant_new_string("command"));
    GVariant* value = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, packet.data(),
                                                packet.size(), sizeof(uint8_t));
    GVariant* parameters = g_variant_new("(@ay@a{sv})", value,
                                         g_variant_builder_end(&options));
    GError* error = nullptr;
    if (!call_method(impl_->bus, impl_->characteristic_path.c_str(),
                     "org.bluez.GattCharacteristic1", "WriteValue", parameters, &error)) {
        print_error("send key state over BLE", error);
        g_clear_error(&error);
        return false;
    }
    return true;
}

} // namespace presserator::host
