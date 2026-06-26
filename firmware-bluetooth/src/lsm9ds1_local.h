#pragma once

#include <stdbool.h>

bool lsm9ds1_local_init();
bool lsm9ds1_local_read_imu(float *ax, float *ay, float *az, float *gx, float *gy, float *gz);
bool lsm9ds1_local_read_mag(float *mx, float *my, float *mz);
