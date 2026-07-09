#include "ble_button_led.h"
#include "ble_nus.h"
#include "ble_shared.h"
#include "flash_layout.h"
#include "descriptor_parser.h"
#include "globals.h"
#include "remapper.h"
#include "tick.h"

#include <string.h>

#include <pico/cyw43_arch.h>

extern "C" {
#include "ble/att_server.h"
#include "ble/le_device_db.h"
#include "ble_remapper.h"
#include "btstack.h"
#include "gap.h"
}

extern "C" uint32_t pico_flash_bank_get_storage_offset_func(void) {
    return REMAPPER_FLASH_BANK_STORAGE_OFFSET;
}

static uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset,
                                  uint8_t* buffer, uint16_t buffer_size) {
    (void) con_handle;
    (void) attribute_handle;
    (void) offset;
    (void) buffer;
    (void) buffer_size;
    return 0;
}

static int att_write_callback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode,
                              uint16_t offset, uint8_t* buffer, uint16_t buffer_size) {
    (void) con_handle;
    (void) attribute_handle;
    (void) transaction_mode;
    (void) offset;
    (void) buffer;
    (void) buffer_size;
    return 0;
}

void extra_init() {
    ble_queues_init();

    if (cyw43_arch_init()) {
        return;
    }

    att_server_init(profile_data, att_read_callback, att_write_callback);
    ble_nus_init();

    hci_power_control(HCI_POWER_ON);
    ble_button_led_init();
}

uint32_t get_gpio_valid_pins_mask() {
    return GPIO_VALID_PINS_BASE & ~(
#ifdef PICO_DEFAULT_UART_TX_PIN
        (1u << PICO_DEFAULT_UART_TX_PIN) |
#endif
#ifdef PICO_DEFAULT_UART_RX_PIN
        (1u << PICO_DEFAULT_UART_RX_PIN) |
#endif
        0);
}

void read_report(bool* new_report, bool* tick) {
    *tick = get_and_clear_tick_pending();
    *new_report = false;

    ble_button_led_poll();

    struct ble_report_t incoming_report;
    if (queue_try_remove(&ble_report_queue, &incoming_report)) {
        handle_received_report(incoming_report.data, incoming_report.len,
                               incoming_report.interface, incoming_report.external_report_id);
        *new_report = true;
    }
}

void interval_override_updated() {
}

void flash_b_side() {
}

static bool addr_is_zero(const bd_addr_t addr) {
    for (int i = 0; i < 6; i++) {
        if (addr[i] != 0) {
            return false;
        }
    }
    return true;
}

static bool same_peer_addr(const ble_peer_info_t* peer, int addr_type, const bd_addr_t addr) {
    return (peer->addr_type == addr_type) && (memcmp(peer->addr, addr, 6) == 0);
}

static int ble_db_valid_count() {
    int count = 0;
    for (int i = 0; i < le_device_db_max_count(); i++) {
        int addr_type;
        bd_addr_t addr;
        le_device_db_info(i, &addr_type, addr, NULL);
        if ((addr_type != BD_ADDR_TYPE_UNKNOWN) && !addr_is_zero(addr)) {
            count++;
        }
    }
    return count;
}

static bool ble_db_contains_peer(const ble_peer_info_t* peer) {
    if (peer->addr_type == BD_ADDR_TYPE_UNKNOWN) {
        return false;
    }
    for (int i = 0; i < le_device_db_max_count(); i++) {
        int addr_type;
        bd_addr_t addr;
        le_device_db_info(i, &addr_type, addr, NULL);
        if ((addr_type != BD_ADDR_TYPE_UNKNOWN) && same_peer_addr(peer, addr_type, addr)) {
            return true;
        }
    }
    return false;
}

static bool fill_db_peer_by_visible_index(uint32_t requested_index, uint8_t total_count, ble_peer_info_t* peer_info, const ble_peer_info_t* active_nus) {
    uint32_t visible_index = 0;
    for (int i = 0; i < le_device_db_max_count(); i++) {
        int addr_type;
        bd_addr_t addr;
        le_device_db_info(i, &addr_type, addr, NULL);
        if ((addr_type == BD_ADDR_TYPE_UNKNOWN) || addr_is_zero(addr)) {
            continue;
        }
        if (visible_index == requested_index) {
            if ((active_nus != NULL) && same_peer_addr(active_nus, addr_type, addr)) {
                *peer_info = *active_nus;
                peer_info->total_count = total_count;
                peer_info->flags |= BLE_PEER_FLAG_BONDED;
                peer_info->port = i + 1;
                return true;
            }
            memset(peer_info, 0, sizeof(*peer_info));
            peer_info->present = 1;
            peer_info->total_count = total_count;
            peer_info->kind = BlePeerKind::UNKNOWN;
            peer_info->flags = BLE_PEER_FLAG_BONDED;
            peer_info->addr_type = addr_type;
            memcpy(peer_info->addr, addr, sizeof(peer_info->addr));
            peer_info->port = i + 1;
            return true;
        }
        visible_index++;
    }
    return false;
}

bool get_ble_peer_info(uint32_t index, uint32_t name_offset, ble_peer_info_t* peer_info) {
    ble_peer_info_t active_nus;
    bool have_active_nus = ble_nus_get_peer_info(&active_nus, name_offset);
    int db_count = ble_db_valid_count();
    bool active_is_in_db = have_active_nus && ble_db_contains_peer(&active_nus);
    uint8_t total_count = db_count + ((have_active_nus && !active_is_in_db) ? 1 : 0);

    memset(peer_info, 0, sizeof(*peer_info));
    peer_info->total_count = total_count;

    if (have_active_nus && !active_is_in_db) {
        if (index == 0) {
            *peer_info = active_nus;
            peer_info->total_count = total_count;
            return true;
        }
        index--;
    }

    if (fill_db_peer_by_visible_index(index, total_count, peer_info, have_active_nus ? &active_nus : NULL)) {
        return true;
    }

    peer_info->present = 0;
    peer_info->total_count = total_count;
    return true;
}

void queue_out_report(uint16_t interface, uint8_t report_id, const uint8_t* buffer, uint8_t len) {
    (void) interface;
    (void) report_id;
    (void) buffer;
    (void) len;
}

void queue_set_feature_report(uint16_t interface, uint8_t report_id, const uint8_t* buffer, uint8_t len) {
    (void) interface;
    (void) report_id;
    (void) buffer;
    (void) len;
}

void queue_get_feature_report(uint16_t interface, uint8_t report_id, uint8_t len) {
    (void) interface;
    (void) report_id;
    (void) len;
}

void send_out_report() {
}

void sof_callback() {
    set_tick_pending();
}
