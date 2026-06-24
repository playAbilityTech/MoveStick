#ifndef _NUS_PROTOCOL_H_
#define _NUS_PROTOCOL_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NUS_PROTOCOL_VERSION 1
#define NUS_PACKET_BUFFER_SIZE 512

#define NUS_SLIP_END 0300
#define NUS_SLIP_ESC 0333
#define NUS_SLIP_ESC_END 0334
#define NUS_SLIP_ESC_ESC 0335

#define NUS_VIRTUAL_INTERFACE 0x7f00
#define NUS_VIRTUAL_VID 0x0f0d
#define NUS_VIRTUAL_PID 0x00c1

struct __attribute__((packed)) nus_packet_t {
    uint8_t protocol_version;
    uint8_t our_descriptor_number;
    uint8_t len;
    uint8_t report_id;
    uint8_t data[0];
};

typedef void (*nus_report_cb_t)(uint8_t report_id, const uint8_t* payload, uint8_t payload_len, void* user_data);

typedef struct {
    uint8_t buffer[NUS_PACKET_BUFFER_SIZE];
    uint16_t bytes_read;
    bool escaped;
    bool overflowed;
    uint8_t last_descriptor_warning;
} nus_decoder_t;

void nus_decoder_init(nus_decoder_t* decoder);
void nus_decoder_reset(nus_decoder_t* decoder);
void nus_decoder_process_bytes(nus_decoder_t* decoder, const uint8_t* data, uint16_t len,
                               nus_report_cb_t cb, void* user_data);

void nus_init_virtual_device(void);

void nus_log_packet_too_small(uint16_t len);
void nus_log_invalid_packet(uint8_t protocol_version, uint8_t len, uint16_t payload_len,
                            uint8_t our_descriptor_number, uint8_t report_id);
void nus_log_descriptor_mismatch(uint8_t nus_descriptor, uint8_t active_descriptor);
void nus_log_packet_too_large(void);
void nus_log_crc_error(uint32_t expected_crc, uint32_t received_crc);

#ifdef __cplusplus
}
#endif

#endif
