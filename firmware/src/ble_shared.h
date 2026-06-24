#ifndef _BLE_SHARED_H_
#define _BLE_SHARED_H_

#include <stdint.h>

#include "nus_protocol.h"
#include "pico/util/queue.h"

#define BLE_MAX_CENTRAL_CONNECTIONS 4
#define BLE_SCAN_DELAY_MS 1000
#define BLE_CLEAR_BONDS_BUTTON_PRESS_MS 3000

#define BLE_NUS_VIRTUAL_INTERFACE NUS_VIRTUAL_INTERFACE
#define BLE_NUS_VIRTUAL_VID NUS_VIRTUAL_VID
#define BLE_NUS_VIRTUAL_PID NUS_VIRTUAL_PID

struct ble_report_t {
    uint16_t interface;
    uint8_t external_report_id;
    uint8_t len;
    uint8_t data[65];
};

struct ble_descriptor_t {
    uint16_t size;
    uint8_t conn_idx;
    uint8_t data[512];
};

struct ble_disconnected_t {
    uint8_t conn_idx;
};

extern queue_t ble_report_queue;
extern queue_t ble_descriptor_queue;
extern queue_t ble_disconnected_queue;

void ble_queues_init(void);
bool ble_report_queue_try_add(const struct ble_report_t* report);
bool ble_descriptor_queue_try_add(const struct ble_descriptor_t* descriptor);
bool ble_disconnected_queue_try_add(uint8_t conn_idx);

#endif
