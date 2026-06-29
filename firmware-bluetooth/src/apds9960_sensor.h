#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/devicetree.h>

#define APDS9960_AVAILABLE DT_NODE_EXISTS(DT_NODELABEL(apds9960))

typedef enum {
    APDS9960_GESTURE_NONE = 0,
    APDS9960_GESTURE_UP,
    APDS9960_GESTURE_DOWN,
    APDS9960_GESTURE_LEFT,
    APDS9960_GESTURE_RIGHT,
} apds9960_gesture_t;

typedef struct {
    uint8_t proximity;
    uint16_t clear;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    int16_t gesture_x;
    int16_t gesture_y;
    uint16_t gesture_strength;
    apds9960_gesture_t gesture;
} apds9960_sensor_data_t;

bool apds9960_sensor_init(void);
bool apds9960_sensor_read(apds9960_sensor_data_t* data);
