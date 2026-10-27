#include "usb.h"

#include <atomic>

#include "esp_log.h"
#include "keyboard.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

namespace {

constexpr char kTag[] = "usb";
std::atomic<bool> g_suspended{false};

const tusb_desc_device_t kDeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xCAFE, // Development placeholder; replace before distribution.
    .idProduct = 0x4001, // Development placeholder; replace before distribution.
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

const char kLanguageDescriptor[] = {0x09, 0x04};
const char* kStringDescriptors[] = {
    kLanguageDescriptor,
    "Presserator",
    "USB Keyboard",
};

constexpr uint8_t kConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN,
                          0, 100), // Bus-powered, 100 mA.
    TUD_HID_DESCRIPTOR(0, 0, true, presserator::kKeyboardReportDescriptorLength,
                       0x81, 8, 10),
};

static_assert(sizeof(kConfigurationDescriptor) == TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN,
              "Configuration descriptor length must match its contents");

void device_event_handler(tinyusb_event_t* event, void*) {
    if (event == nullptr) {
        return;
    }

    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        ESP_LOGI(kTag, "USB attached");
        break;
    case TINYUSB_EVENT_DETACHED:
        g_suspended.store(false);
        presserator::keyboard_init();
        ESP_LOGI(kTag, "USB detached");
        break;
    case TINYUSB_EVENT_SUSPENDED:
        g_suspended.store(true);
        ESP_LOGI(kTag, "USB suspended");
        break;
    case TINYUSB_EVENT_RESUMED:
        g_suspended.store(false);
        ESP_LOGI(kTag, "USB resumed");
        break;
    default:
        break;
    }
}

} // namespace

esp_err_t usb_init() {
    presserator::keyboard_init();

    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(device_event_handler);
    config.descriptor.device = &kDeviceDescriptor;
    config.descriptor.string = kStringDescriptors;
    config.descriptor.string_count = sizeof(kStringDescriptors) / sizeof(kStringDescriptors[0]);
    config.descriptor.full_speed_config = kConfigurationDescriptor;

    const esp_err_t err = tinyusb_driver_install(&config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "TinyUSB device stack install failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(kTag, "TinyUSB HID keyboard stack initialized");
    return ESP_OK;
}

bool usb_is_mounted() {
    return tud_mounted();
}

bool usb_is_suspended() {
    return g_suspended.load();
}

