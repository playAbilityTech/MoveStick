#ifndef _IMU_DESCRIPTOR_H_
#define _IMU_DESCRIPTOR_H_

#include <stdint.h>

static const uint8_t imu_hid_report_desc_6dof[] = {
    0x05, 0x20,        // Usage Page (Sensor)
    0x09, 0x8A,        // Usage (Motion: Orientation)
    0xA1, 0x01,        // Collection (Application)

    // Pitch (rotation around X-axis)
    0x05, 0x20,        //   Usage Page (Sensor)
    0x09, 0x8E,        //   Usage (Orientation: Pitch)
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (+32767)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x14,        //   Unit (Degrees)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Roll (rotation around Y-axis)
    0x09, 0x8F,        //   Usage (Orientation: Roll)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Acceleration Magnitude
    0x05, 0x20,        //   Usage Page (Sensor)
    0x09, 0x73,        //   Usage (Motion: Acceleration)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x66, 0x14, 0xF0,  //   Unit (m/s^2)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    0xC0,              // End Collection
};

static const uint8_t imu_hid_report_desc_9dof[] = {
    0x05, 0x20,        // Usage Page (Sensor)
    0x09, 0x8A,        // Usage (Motion: Orientation)
    0xA1, 0x01,        // Collection (Application)

    // Yaw (rotation around Z-axis)
    0x05, 0x20,        //   Usage Page (Sensor)
    0x09, 0x8D,        //   Usage (Orientation: Yaw)
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (+32767)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x14,        //   Unit (Degrees)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Pitch (rotation around X-axis)
    0x09, 0x8E,        //   Usage (Orientation: Pitch)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Roll (rotation around Y-axis)
    0x09, 0x8F,        //   Usage (Orientation: Roll)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Acceleration Magnitude
    0x05, 0x20,        //   Usage Page (Sensor)
    0x09, 0x73,        //   Usage (Motion: Acceleration)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x66, 0x14, 0xF0,  //   Unit (m/s^2)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    0xC0,              // End Collection
};

#define IMU_HID_REPORT_DESC_6DOF_SIZE sizeof(imu_hid_report_desc_6dof)
#define IMU_HID_REPORT_DESC_9DOF_SIZE sizeof(imu_hid_report_desc_9dof)

typedef struct {
    int16_t pitch;
    int16_t roll;
    uint16_t magnitude;
} __attribute__((packed)) imu_report_6dof_t;

typedef struct {
    int16_t yaw;
    int16_t pitch;
    int16_t roll;
    uint16_t magnitude;
} __attribute__((packed)) imu_report_9dof_t;

#endif // _IMU_DESCRIPTOR_H_
