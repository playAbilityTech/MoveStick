#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/conn.h>

typedef bool (*ble_nus_report_submit_t)(uint16_t interface, uint8_t external_report_id,
                                        const uint8_t* payload, uint8_t payload_len);

void ble_nus_init(ble_nus_report_submit_t report_submit);
void ble_nus_start_advertising(void);
bool ble_nus_handle_connected(struct bt_conn* conn);
bool ble_nus_handle_disconnected(struct bt_conn* conn, uint8_t reason);
bool ble_nus_handle_security_changed(struct bt_conn* conn, bt_security_t level,
                                     enum bt_security_err err);
bool ble_nus_get_peer(bt_addr_le_t* addr, bool* encrypted);
void ble_nus_usb_report_sent(uint8_t interface, bool sent);
