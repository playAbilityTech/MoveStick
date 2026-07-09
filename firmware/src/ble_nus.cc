#include "ble_nus.h"

#include "ble_button_led.h"
#include "ble_shared.h"
#include "nus_protocol.h"

#include <string.h>

#include <pico/cyw43_arch.h>

extern "C" {
#include "ble/att_server.h"
#include "ble/gatt-service/nordic_spp_service_server.h"
#include "ble/sm.h"
#include "btstack.h"
#include "gap.h"
}

static nus_decoder_t nus_decoder;
static hci_con_handle_t nus_conn = HCI_CON_HANDLE_INVALID;
static hci_con_handle_t nus_gap_conn = HCI_CON_HANDLE_INVALID;
static bool nus_addr_known = false;
static uint8_t nus_addr_type = 0;
static bd_addr_t nus_addr;
static bool nus_advertising = false;
static btstack_packet_callback_registration_t nus_hci_event_callback_registration;

static uint8_t adv_data[] = {
    0x02, 0x01, 0x06,
    0x11, 0x07, 0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e,
};

static uint8_t scan_rsp_data[] = {
    0x0c, 0x09, 'H', 'I', 'D', ' ', 'R', 'e', 'm', 'a', 'p', 'p', 'e', 'r',
};

static void nus_report_received(uint8_t report_id, const uint8_t* payload, uint8_t payload_len, void* user_data) {
    (void) user_data;

    struct ble_report_t report = {
        .interface = BLE_NUS_VIRTUAL_INTERFACE,
        .external_report_id = report_id,
        .len = payload_len,
    };
    memcpy(report.data, payload, payload_len);
    ble_report_queue_try_add(&report);
}

static void nus_process_bytes(const uint8_t* data, uint16_t len) {
    ble_status_led_activity_flash();
    nus_decoder_process_bytes(&nus_decoder, data, len, nus_report_received, NULL);
}

static void nus_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size) {
    if (packet_type == RFCOMM_DATA_PACKET) {
        nus_process_bytes(packet, size);
        return;
    }

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_LE_META:
            if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_CONNECTION_COMPLETE &&
                hci_subevent_le_connection_complete_get_status(packet) == 0 &&
                hci_subevent_le_connection_complete_get_role(packet) == 1) {
                nus_gap_conn = hci_subevent_le_connection_complete_get_connection_handle(packet);
                nus_addr_type = hci_subevent_le_connection_complete_get_peer_address_type(packet);
                hci_subevent_le_connection_complete_get_peer_address(packet, nus_addr);
                nus_addr_known = true;
            }
            break;
        case HCI_EVENT_GATTSERVICE_META:
            switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
                case GATTSERVICE_SUBEVENT_SPP_SERVICE_CONNECTED:
                    nus_conn = gattservice_subevent_spp_service_connected_get_con_handle(packet);
                    sm_request_pairing(nus_conn);
                    break;
                case GATTSERVICE_SUBEVENT_SPP_SERVICE_DISCONNECTED:
                    if (nus_conn == gattservice_subevent_spp_service_disconnected_get_con_handle(packet)) {
                        nus_conn = HCI_CON_HANDLE_INVALID;
                        nus_decoder_reset(&nus_decoder);
                        ble_nus_start_advertising();
                    }
                    break;
                default:
                    break;
            }
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            if (nus_conn == hci_event_disconnection_complete_get_connection_handle(packet)) {
                nus_conn = HCI_CON_HANDLE_INVALID;
                nus_decoder_reset(&nus_decoder);
                ble_nus_start_advertising();
            }
            if (nus_gap_conn == hci_event_disconnection_complete_get_connection_handle(packet)) {
                nus_gap_conn = HCI_CON_HANDLE_INVALID;
                nus_addr_known = false;
            }
            break;
        default:
            break;
    }
}

void ble_nus_init(void) {
    nus_decoder_init(&nus_decoder);
    nordic_spp_service_server_init(nus_packet_handler);
    nus_hci_event_callback_registration.callback = &nus_packet_handler;
    hci_add_event_handler(&nus_hci_event_callback_registration);
    nus_init_virtual_device();
    ble_nus_start_advertising();
}

void ble_nus_start_advertising(void) {
    if (nus_advertising) {
        return;
    }
    gap_advertisements_set_data(sizeof(adv_data), adv_data);
    gap_scan_response_set_data(sizeof(scan_rsp_data), scan_rsp_data);
    gap_advertisements_set_params(0x0030, 0x0030, 0, 0, NULL, 0x07, 0);
    gap_advertisements_enable(1);
    nus_advertising = true;
}

void ble_nus_stop_advertising(void) {
    gap_advertisements_enable(0);
    nus_advertising = false;
}

void ble_nus_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size) {
    nus_packet_handler(packet_type, channel, packet, size);
}

bool ble_nus_is_peripheral_connection(uint8_t packet_type, uint8_t* packet, uint16_t size) {
    if (packet_type != HCI_EVENT_PACKET || size == 0) {
        return false;
    }
    if (hci_event_packet_get_type(packet) != HCI_EVENT_LE_META) {
        return false;
    }
    if (hci_event_le_meta_get_subevent_code(packet) != HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
        return false;
    }
    return hci_subevent_le_connection_complete_get_role(packet) == 1;
}

bool ble_nus_get_peer_info(ble_peer_info_t* peer_info, uint32_t name_offset) {
    (void) name_offset;

    if (nus_conn == HCI_CON_HANDLE_INVALID && nus_gap_conn == HCI_CON_HANDLE_INVALID) {
        return false;
    }

    memset(peer_info, 0, sizeof(*peer_info));
    peer_info->present = 1;
    peer_info->total_count = 1;
    peer_info->kind = BlePeerKind::NUS;
    peer_info->flags = BLE_PEER_FLAG_CONNECTED;
    hci_con_handle_t conn = nus_conn != HCI_CON_HANDLE_INVALID ? nus_conn : nus_gap_conn;
    if (gap_bonded(conn)) {
        peer_info->flags |= BLE_PEER_FLAG_BONDED;
    }
    if (gap_security_level(conn) >= LEVEL_2) {
        peer_info->flags |= BLE_PEER_FLAG_ENCRYPTED;
    }
    if (nus_addr_known) {
        peer_info->addr_type = nus_addr_type;
        memcpy(peer_info->addr, nus_addr, sizeof(peer_info->addr));
    }
    peer_info->port = 0xFF;
    return true;
}
