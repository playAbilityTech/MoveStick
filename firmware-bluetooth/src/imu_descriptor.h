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

    // Microphone level
    0x06, 0xFA, 0xFF,  //   Usage Page (Vendor-defined 0xFFFA)
    0x09, 0x01,        //   Usage (Mic Level)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0x78, 0x00,  //   Logical Maximum (120)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x00,        //   Unit (None)
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

    // Microphone level
    0x06, 0xFA, 0xFF,  //   Usage Page (Vendor-defined 0xFFFA)
    0x09, 0x01,        //   Usage (Mic Level)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0x78, 0x00,  //   Logical Maximum (120)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x00,        //   Unit (None)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    0xC0,              // End Collection
};

static const uint8_t imu_hid_report_desc_6dof_apds[] = {
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

    // Microphone level
    0x06, 0xFA, 0xFF,  //   Usage Page (Vendor-defined 0xFFFA)
    0x09, 0x01,        //   Usage (Mic Level)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0x78, 0x00,  //   Logical Maximum (120)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x00,        //   Unit (None)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 proximity
    0x09, 0x02,        //   Usage (Proximity)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x00,        //   Unit (None)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 ambient light
    0x09, 0x03,        //   Usage (Ambient Light)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 gesture axes
    0x09, 0x04,        //   Usage (Gesture X)
    0x09, 0x05,        //   Usage (Gesture Y)
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (+32767)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x02,        //   Report Count (2)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x00,        //   Unit (None)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 gesture strength
    0x09, 0x06,        //   Usage (Gesture Strength)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 buttons: up, down, left, right, near, covered
    0x19, 0x10,        //   Usage Minimum (Gesture Up)
    0x29, 0x15,        //   Usage Maximum (Covered)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1 bit)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x75, 0x01,        //   Report Size (1 bit)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x03,        //   Input (Const,Var,Abs)

    0xC0,              // End Collection
};

static const uint8_t imu_hid_report_desc_9dof_apds[] = {
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

    // Microphone level
    0x06, 0xFA, 0xFF,  //   Usage Page (Vendor-defined 0xFFFA)
    0x09, 0x01,        //   Usage (Mic Level)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0x78, 0x00,  //   Logical Maximum (120)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x00,        //   Unit (None)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 proximity
    0x09, 0x02,        //   Usage (Proximity)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x00,        //   Unit (None)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 ambient light
    0x09, 0x03,        //   Usage (Ambient Light)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 gesture axes
    0x09, 0x04,        //   Usage (Gesture X)
    0x09, 0x05,        //   Usage (Gesture Y)
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (+32767)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x02,        //   Report Count (2)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x00,        //   Unit (None)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 gesture strength
    0x09, 0x06,        //   Usage (Gesture Strength)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 buttons: up, down, left, right, near, covered
    0x19, 0x10,        //   Usage Minimum (Gesture Up)
    0x29, 0x15,        //   Usage Maximum (Covered)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1 bit)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x75, 0x01,        //   Report Size (1 bit)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x03,        //   Input (Const,Var,Abs)

    0xC0,              // End Collection
};

static const uint8_t apds9960_hid_report_desc[] = {
    0x06, 0xFA, 0xFF,  // Usage Page (Vendor-defined 0xFFFA)
    0x09, 0x20,        // Usage (APDS9960)
    0xA1, 0x01,        // Collection (Application)

    // APDS9960 proximity
    0x09, 0x02,        //   Usage (Proximity)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x00,        //   Unit (None)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 ambient light
    0x09, 0x03,        //   Usage (Ambient Light)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 gesture axes
    0x09, 0x04,        //   Usage (Gesture X)
    0x09, 0x05,        //   Usage (Gesture Y)
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (+32767)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x02,        //   Report Count (2)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x00,        //   Unit (None)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 gesture strength
    0x09, 0x06,        //   Usage (Gesture Strength)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // APDS9960 buttons: up, down, left, right, near, covered
    0x19, 0x10,        //   Usage Minimum (Gesture Up)
    0x29, 0x15,        //   Usage Maximum (Covered)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1 bit)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x75, 0x01,        //   Report Size (1 bit)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x03,        //   Input (Const,Var,Abs)

    0xC0,              // End Collection
};

#define IMU_HID_REPORT_DESC_6DOF_SIZE sizeof(imu_hid_report_desc_6dof)
#define IMU_HID_REPORT_DESC_9DOF_SIZE sizeof(imu_hid_report_desc_9dof)
#define IMU_HID_REPORT_DESC_6DOF_APDS_SIZE sizeof(imu_hid_report_desc_6dof_apds)
#define IMU_HID_REPORT_DESC_9DOF_APDS_SIZE sizeof(imu_hid_report_desc_9dof_apds)
#define APDS9960_HID_REPORT_DESC_SIZE sizeof(apds9960_hid_report_desc)

typedef struct {
    int16_t pitch;
    int16_t roll;
    uint16_t magnitude;
    uint16_t mic_level;
} __attribute__((packed)) imu_report_6dof_t;

typedef struct {
    int16_t yaw;
    int16_t pitch;
    int16_t roll;
    uint16_t magnitude;
    uint16_t mic_level;
} __attribute__((packed)) imu_report_9dof_t;

typedef struct {
    int16_t pitch;
    int16_t roll;
    uint16_t magnitude;
    uint16_t mic_level;
    uint16_t proximity;
    uint16_t ambient_light;
    int16_t gesture_x;
    int16_t gesture_y;
    uint16_t gesture_strength;
    uint8_t apds_buttons;
} __attribute__((packed)) imu_report_6dof_apds_t;

typedef struct {
    int16_t yaw;
    int16_t pitch;
    int16_t roll;
    uint16_t magnitude;
    uint16_t mic_level;
    uint16_t proximity;
    uint16_t ambient_light;
    int16_t gesture_x;
    int16_t gesture_y;
    uint16_t gesture_strength;
    uint8_t apds_buttons;
} __attribute__((packed)) imu_report_9dof_apds_t;

typedef struct {
    uint16_t proximity;
    uint16_t ambient_light;
    int16_t gesture_x;
    int16_t gesture_y;
    uint16_t gesture_strength;
    uint8_t apds_buttons;
} __attribute__((packed)) apds9960_report_t;

#endif // _IMU_DESCRIPTOR_H_
