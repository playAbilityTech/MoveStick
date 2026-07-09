#include "ble_nus.h"

#include "activity_led.h"
#include "nus_protocol.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(remapper, LOG_LEVEL_DBG);

static struct bt_uuid_128 nus_service_uuid = BT_UUID_INIT_128(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static struct bt_uuid_128 nus_rx_uuid = BT_UUID_INIT_128(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static struct bt_uuid_128 nus_tx_uuid = BT_UUID_INIT_128(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static nus_decoder_t nus_decoder;
static struct bt_conn* nus_conn;
static bool nus_encrypted = false;
static ble_nus_report_submit_t submit_report;

#ifndef CONFIG_REMAPPER_NUS_LATENCY_INSTRUMENTATION
#define CONFIG_REMAPPER_NUS_LATENCY_INSTRUMENTATION 1
#endif

#define NUS_LATENCY_INSTRUMENTATION CONFIG_REMAPPER_NUS_LATENCY_INSTRUMENTATION
#if NUS_LATENCY_INSTRUMENTATION
static uint32_t nus_rx_cycles = 0;
static bool nus_latency_pending = false;
static uint32_t nus_latency_samples = 0;
#endif

extern "C" void nus_log_packet_too_small(uint16_t len) {
    LOG_WRN("NUS packet too small: %d", len);
}

extern "C" void nus_log_invalid_packet(uint8_t protocol_version, uint8_t len, uint16_t payload_len,
                                       uint8_t our_descriptor_number, uint8_t report_id) {
    LOG_WRN("Invalid NUS packet: proto=%d len=%d payload=%d desc=%d report_id=%d",
            protocol_version, len, payload_len, our_descriptor_number, report_id);
}

extern "C" void nus_log_descriptor_mismatch(uint8_t nus_descriptor, uint8_t active_descriptor) {
    LOG_WRN("NUS descriptor %d does not match active USB descriptor %d",
            nus_descriptor, active_descriptor);
}

extern "C" void nus_log_packet_too_large(void) {
    LOG_WRN("NUS packet too large; dropping until frame end");
}

extern "C" void nus_log_crc_error(uint32_t expected_crc, uint32_t received_crc) {
    LOG_WRN("NUS CRC error: expected 0x%08X, got 0x%08X", expected_crc, received_crc);
}

static void nus_report_received(uint8_t report_id, const uint8_t* payload, uint8_t payload_len, void* user_data) {
    (void) user_data;

    if (!submit_report ||
        !submit_report(NUS_VIRTUAL_INTERFACE, report_id, payload, payload_len)) {
        LOG_WRN("Dropped NUS report: report queue full");
        return;
    }

#if NUS_LATENCY_INSTRUMENTATION
    nus_rx_cycles = k_cycle_get_32();
    nus_latency_pending = true;
#endif
}

static void nus_process_bytes(const uint8_t* data, uint16_t len) {
    activity_led_flash(50);
    nus_decoder_process_bytes(&nus_decoder, data, len, nus_report_received, NULL);
}

static ssize_t nus_rx_write_cb(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                               const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (conn == nus_conn) {
        nus_process_bytes((const uint8_t*) buf, len);
    }
    return len;
}

static void nus_ccc_changed(const struct bt_gatt_attr* attr, uint16_t value) {
    LOG_DBG("NUS CCC changed: %d", value);
}

BT_GATT_SERVICE_DEFINE(nus_service,
    BT_GATT_PRIMARY_SERVICE(&nus_service_uuid),
    BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, nus_rx_write_cb, NULL),
    BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(nus_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

void ble_nus_init(ble_nus_report_submit_t report_submit) {
    submit_report = report_submit;
    nus_decoder_init(&nus_decoder);
    nus_init_virtual_device();
}

void ble_nus_start_advertising(void) {
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_CONNECTABLE,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .peer = NULL,
    };

    static const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA_BYTES(BT_DATA_UUID128_ALL,
            0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
            0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e),
    };

    static const struct bt_data sd[] = {
        BT_DATA(BT_DATA_NAME_COMPLETE, "HID Remapper", sizeof("HID Remapper") - 1),
    };

    int err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err && err != -EALREADY) {
        LOG_ERR("bt_le_adv_start returned %d", err);
        return;
    }
    LOG_INF("NUS advertising started.");
}

bool ble_nus_handle_connected(struct bt_conn* conn) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) || info.role != BT_CONN_ROLE_PERIPHERAL) {
        return false;
    }

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!nus_conn) {
        nus_conn = bt_conn_ref(conn);
        LOG_INF("NUS client connected: %s", addr);
        int err = bt_conn_set_security(conn, BT_SECURITY_L2);
        if (err) {
            LOG_ERR("bt_conn_set_security returned %d", err);
        }
    } else {
        LOG_WRN("Rejecting extra NUS client: %s", addr);
        int err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        if (err) {
            LOG_ERR("bt_conn_disconnect returned %d", err);
        }
    }

    return true;
}

bool ble_nus_handle_disconnected(struct bt_conn* conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];
    struct bt_conn_info info;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn == nus_conn) {
        LOG_INF("NUS client disconnected: %s (reason=%u)", addr, reason);
        bt_conn_unref(nus_conn);
        nus_conn = NULL;
        nus_encrypted = false;
        nus_decoder_reset(&nus_decoder);
#if NUS_LATENCY_INSTRUMENTATION
        nus_latency_pending = false;
#endif
        ble_nus_start_advertising();
        return true;
    }

    if (bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_PERIPHERAL) {
        LOG_INF("Peripheral client disconnected: %s (reason=%u)", addr, reason);
        return true;
    }

    return false;
}

static void nus_optimize_connection(struct bt_conn* conn) {
    struct bt_le_conn_param param = {
        .interval_min = 6,
        .interval_max = 9,
        .latency = 0,
        .timeout = 400,
    };
    int err = bt_conn_le_param_update(conn, &param);
    if (err) {
        LOG_WRN("NUS conn param update failed: %d", err);
    } else {
        LOG_INF("NUS conn param update requested (7.5-11.25 ms)");
    }

#if defined(CONFIG_BT_CTLR_PHY_2M)
    struct bt_conn_le_phy_param phy = BT_CONN_LE_PHY_PARAM_INIT(BT_GAP_LE_PHY_2M,
                                                                  BT_GAP_LE_PHY_2M);
    err = bt_conn_le_phy_update(conn, &phy);
    if (err) {
        LOG_WRN("NUS 2M PHY update failed: %d", err);
    }
#endif
}

bool ble_nus_handle_security_changed(struct bt_conn* conn, bt_security_t level,
                                     enum bt_security_err err) {
    if (conn == nus_conn) {
        if (!err && level >= BT_SECURITY_L2) {
            nus_encrypted = true;
            nus_optimize_connection(conn);
        } else if (err) {
            nus_encrypted = false;
            LOG_ERR("NUS security failed: level=%u, err=%d", level, err);
        }
        return true;
    }

    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_PERIPHERAL) {
        return true;
    }

    return false;
}

bool ble_nus_get_peer(bt_addr_le_t* addr, bool* encrypted) {
    if (!nus_conn) {
        return false;
    }
    bt_addr_le_copy(addr, bt_conn_get_dst(nus_conn));
    *encrypted = nus_encrypted;
    return true;
}

void ble_nus_usb_report_sent(uint8_t interface, bool sent) {
#if NUS_LATENCY_INSTRUMENTATION
    if (sent && interface == 0 && nus_latency_pending) {
        uint32_t delta = k_cycle_get_32() - nus_rx_cycles;
        nus_latency_pending = false;
        if ((nus_latency_samples++ & 0x3f) == 0) {
            LOG_INF("NUS RX->USB latency: %u us", k_cyc_to_us_near32(delta));
        }
    }
#else
    (void) interface;
    (void) sent;
#endif
}
