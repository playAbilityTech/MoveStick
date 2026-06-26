#pragma once

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include "globals.h"

#define IMU_HAS_LSM6DS3TR_C DT_NODE_EXISTS(DT_NODELABEL(lsm6ds3tr_c))
#define IMU_HAS_LSM9DS1_LOCAL (DT_NODE_EXISTS(DT_NODELABEL(lsm9ds1)) && DT_NODE_EXISTS(DT_NODELABEL(lsm9ds1_mag)))
#define IMU_HAS_MAGNETOMETER IMU_HAS_LSM9DS1_LOCAL
#define IMU_AVAILABLE (IMU_HAS_LSM6DS3TR_C || IMU_HAS_LSM9DS1_LOCAL)

extern const struct gpio_dt_spec led0;
extern struct k_work_delayable activity_led_off_work;

bool imu_init();
void imu_recalibrate_orientation();
void imu_recalibrate_sensors();
