#include "nus_protocol.h"

#include "crc.h"
#include "descriptor_parser.h"
#include "globals.h"
#include "our_descriptor.h"
#include "remapper.h"

#include <string.h>

extern "C" {

static const uint8_t nus_virtual_gamepad_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x35, 0x00,        //   Physical Minimum (0)
    0x45, 0x01,        //   Physical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0E,        //   Report Count (14)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x0E,        //   Usage Maximum (14)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x01,        //   Input (Const)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x25, 0x0F,        //   Logical Maximum (15)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x65, 0x14,        //   Unit (English Rotation)
    0x09, 0x39,        //   Usage (Hat switch)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null State)
    0x65, 0x00,        //   Unit (None)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Const)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x46, 0xFF, 0x00,  //   Physical Maximum (255)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Const)
    0xC0,              // End Collection
};

__attribute__((weak)) void nus_log_packet_too_small(uint16_t len) {
    (void) len;
}

__attribute__((weak)) void nus_log_invalid_packet(uint8_t protocol_version, uint8_t len, uint16_t payload_len,
                                                  uint8_t our_descriptor_number, uint8_t report_id) {
    (void) protocol_version;
    (void) len;
    (void) payload_len;
    (void) our_descriptor_number;
    (void) report_id;
}

__attribute__((weak)) void nus_log_descriptor_mismatch(uint8_t nus_descriptor, uint8_t active_descriptor) {
    (void) nus_descriptor;
    (void) active_descriptor;
}

__attribute__((weak)) void nus_log_packet_too_large(void) {
}

__attribute__((weak)) void nus_log_crc_error(uint32_t expected_crc, uint32_t received_crc) {
    (void) expected_crc;
    (void) received_crc;
}

void nus_decoder_init(nus_decoder_t* decoder) {
    nus_decoder_reset(decoder);
    decoder->last_descriptor_warning = 0xff;
}

void nus_decoder_reset(nus_decoder_t* decoder) {
    decoder->bytes_read = 0;
    decoder->escaped = false;
    decoder->overflowed = false;
}

static void nus_handle_packet(nus_decoder_t* decoder, const uint8_t* data, uint16_t len,
                              nus_report_cb_t cb, void* user_data) {
    if (len < sizeof(nus_packet_t)) {
        nus_log_packet_too_small(len);
        return;
    }

    const nus_packet_t* msg = (const nus_packet_t*) data;
    uint16_t payload_len = len - sizeof(nus_packet_t);

    if ((msg->protocol_version != NUS_PROTOCOL_VERSION) ||
        (msg->len != payload_len) ||
        (payload_len > 64) ||
        (msg->our_descriptor_number >= NOUR_DESCRIPTORS) ||
        ((msg->report_id == 0) && (payload_len >= 64))) {
        nus_log_invalid_packet(msg->protocol_version, msg->len, payload_len,
                               msg->our_descriptor_number, msg->report_id);
        return;
    }

    if ((msg->our_descriptor_number != our_descriptor_number) &&
        (msg->our_descriptor_number != decoder->last_descriptor_warning)) {
        decoder->last_descriptor_warning = msg->our_descriptor_number;
        nus_log_descriptor_mismatch(msg->our_descriptor_number, our_descriptor_number);
    }

    cb(msg->report_id, msg->data, (uint8_t) payload_len, user_data);
}

static void nus_packet_append(nus_decoder_t* decoder, uint8_t c) {
    if (decoder->bytes_read >= sizeof(decoder->buffer)) {
        if (!decoder->overflowed) {
            nus_log_packet_too_large();
        }
        decoder->overflowed = true;
        return;
    }
    decoder->buffer[decoder->bytes_read++] = c;
}

static void nus_process_byte(nus_decoder_t* decoder, uint8_t c, nus_report_cb_t cb, void* user_data) {
    if (decoder->escaped) {
        switch (c) {
            case NUS_SLIP_ESC_END:
                nus_packet_append(decoder, NUS_SLIP_END);
                break;
            case NUS_SLIP_ESC_ESC:
                nus_packet_append(decoder, NUS_SLIP_ESC);
                break;
            default:
                nus_packet_append(decoder, c);
                break;
        }
        decoder->escaped = false;
        return;
    }

    switch (c) {
        case NUS_SLIP_END:
            if (!decoder->overflowed && (decoder->bytes_read > 4)) {
                uint32_t crc = crc32(decoder->buffer, decoder->bytes_read - 4);
                uint32_t received_crc =
                    (decoder->buffer[decoder->bytes_read - 4] << 0) |
                    (decoder->buffer[decoder->bytes_read - 3] << 8) |
                    (decoder->buffer[decoder->bytes_read - 2] << 16) |
                    (decoder->buffer[decoder->bytes_read - 1] << 24);
                if (crc == received_crc) {
                    nus_handle_packet(decoder, decoder->buffer, decoder->bytes_read - 4, cb, user_data);
                } else {
                    nus_log_crc_error(crc, received_crc);
                }
            }
            nus_decoder_reset(decoder);
            break;
        case NUS_SLIP_ESC:
            if (!decoder->overflowed) {
                decoder->escaped = true;
            }
            break;
        default:
            nus_packet_append(decoder, c);
            break;
    }
}

void nus_decoder_process_bytes(nus_decoder_t* decoder, const uint8_t* data, uint16_t len,
                               nus_report_cb_t cb, void* user_data) {
    for (uint16_t i = 0; i < len; i++) {
        nus_process_byte(decoder, data[i], cb, user_data);
    }
}

void nus_init_virtual_device(void) {
    parse_descriptor(NUS_VIRTUAL_VID, NUS_VIRTUAL_PID,
                     nus_virtual_gamepad_descriptor,
                     sizeof(nus_virtual_gamepad_descriptor),
                     NUS_VIRTUAL_INTERFACE, 0);
    device_connected_callback(NUS_VIRTUAL_INTERFACE, NUS_VIRTUAL_VID, NUS_VIRTUAL_PID, 0);
    their_descriptor_updated = true;
}

}  // extern "C"
