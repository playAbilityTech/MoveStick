#include "ble_button_led.h"
#include "ble_nus.h"
#include "ble_shared.h"
#include "flash_layout.h"
#include "descriptor_parser.h"
#include "globals.h"
#include "remapper.h"
#include "tick.h"

#include <pico/cyw43_arch.h>

extern "C" {
#include "ble/att_server.h"
#include "ble_remapper.h"
#include "btstack.h"
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
