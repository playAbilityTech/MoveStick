#pragma once

#include "apds9960_sensor.h"
#include "motion_sensor.h"

#define SENSOR_INPUTS_AVAILABLE (IMU_AVAILABLE || APDS9960_AVAILABLE)

bool sensor_inputs_init();
void sensor_inputs_recalibrate_orientation();
void sensor_inputs_recalibrate_sensors();
void sensor_inputs_pause_imu();
void sensor_inputs_resume_imu();
