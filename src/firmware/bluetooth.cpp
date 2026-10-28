#include "bluetooth.h"

#include <cstring>

#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "host/ble_store.h"

#include "protocol.h"
#include "receiver.h"

extern "C" void ble_store_config_init(void);

namespace {

constexpr char kTag[] = "ble";
constexpr char kDeviceName[] = "Presserator";
uint16_t g_characteristicHandle = 0;

// UUIDs: b7e6a000-8f0d-4c2e-9f53-9e6c32e70d10 service,
//        b7e6a001-8f0d-4c2e-9f53-9e6c32e70d10 write characteristic.
const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0x10, 0x0D, 0xE7, 0x32, 0x6C, 0x9E, 0x53, 0x9F,
    0x2E, 0x4C, 0x0D, 0x8F, 0x00, 0xA0, 0xE6, 0xB7);
const ble_uuid128_t kCharacteristicUuid = BLE_UUID128_INIT(
    0x10, 0x0D, 0xE7, 0x32, 0x6C, 0x9E, 0x53, 0x9F,
    0x2E, 0x4C, 0x0D, 0x8F, 0x01, 0xA0, 0xE6, 0xB7);

uint8_t g_ownAddressType = 0;

int key_state_access(uint16_t, uint16_t, ble_gatt_access_ctxt* context, void*) {
    if (context == nullptr || context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    uint8_t packet[presserator::protocol::kPacketSize];
    uint16_t length = 0;
    const int rc = ble_hs_mbuf_to_flat(context->om, packet, sizeof(packet), &length);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (length != sizeof(packet)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    const esp_err_t err = presserator::client::protocol_receive(packet, length);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Key report was not sent to USB: %s", esp_err_to_name(err));
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    return 0;
}

const ble_gatt_chr_def kGattCharacteristics[] = {
    {
        .uuid = &kCharacteristicUuid.u,
        .access_cb = key_state_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                 BLE_GATT_CHR_F_WRITE_ENC,
        .min_key_size = 0,
        .val_handle = &g_characteristicHandle,
        .cpfd = nullptr,
    },
    {.uuid = nullptr, .access_cb = nullptr, .arg = nullptr, .descriptors = nullptr,
     .flags = 0, .min_key_size = 0, .val_handle = nullptr, .cpfd = nullptr},
};

const ble_gatt_svc_def kGattServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuid.u,
        .includes = nullptr,
        .characteristics = kGattCharacteristics,
    },
    {.type = 0, .uuid = nullptr, .includes = nullptr, .characteristics = nullptr},
};

void start_advertising() {
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &kServiceUuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) {
        ESP_LOGE(kTag, "Failed to set BLE advertisement fields");
        return;
    }

    ble_hs_adv_fields response{};
    response.name = reinterpret_cast<const uint8_t*>(kDeviceName);
    response.name_len = sizeof(kDeviceName) - 1;
    response.name_is_complete = 1;
    if (ble_gap_adv_rsp_set_fields(&response) != 0) {
        ESP_LOGE(kTag, "Failed to set BLE scan response fields");
        return;
    }

    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    const int rc = ble_gap_adv_start(g_ownAddressType, nullptr, BLE_HS_FOREVER,
                                     &params,
                                     [](ble_gap_event* event, void*) -> int {
                                         switch (event->type) {
                                         case BLE_GAP_EVENT_CONNECT:
                                             if (event->connect.status == 0) {
                                                 ESP_LOGI(kTag, "BLE client connected");
                                             } else {
                                                 start_advertising();
                                             }
                                             break;
                                         case BLE_GAP_EVENT_DISCONNECT:
                                             ESP_LOGI(kTag, "BLE client disconnected");
                                             presserator::client::protocol_reset();
                                             start_advertising();
                                             break;
                                         case BLE_GAP_EVENT_ADV_COMPLETE:
                                             start_advertising();
                                             break;
                                         default:
                                             break;
                                         }
                                         return 0;
                                     },
                                     nullptr);
    if (rc != 0) {
        ESP_LOGE(kTag, "Failed to start BLE advertising: %d", rc);
    } else {
        ESP_LOGI(kTag, "Advertising BLE key input service as %s", kDeviceName);
    }
}

void on_sync() {
    const int rc = ble_hs_id_infer_auto(0, &g_ownAddressType);
    if (rc != 0) {
        ESP_LOGE(kTag, "Could not determine BLE address type: %d", rc);
        return;
    }
    start_advertising();
}

void on_reset(int reason) {
    ESP_LOGE(kTag, "BLE host reset: %d", reason);
}

void host_task(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

} // namespace

esp_err_t bluetooth_init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "NimBLE initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(kDeviceName);

    int rc = ble_gatts_count_cfg(kGattServices);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(kGattServices);
    }
    if (rc != 0) {
        ESP_LOGE(kTag, "BLE GATT service setup failed: %d", rc);
        return ESP_FAIL;
    }

    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
