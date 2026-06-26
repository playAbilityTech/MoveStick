#include "imu.h"
#include "lsm9ds1_local.h"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#if IMU_HAS_LSM9DS1_LOCAL

#define LSM9DS1_AG_WHO_AM_I 0x0f
#define LSM9DS1_AG_CTRL_REG1_G 0x10
#define LSM9DS1_AG_CTRL_REG4 0x1e
#define LSM9DS1_AG_CTRL_REG5_XL 0x1f
#define LSM9DS1_AG_CTRL_REG6_XL 0x20
#define LSM9DS1_AG_CTRL_REG8 0x22
#define LSM9DS1_AG_OUT_X_L_G 0x18
#define LSM9DS1_AG_OUT_X_L_XL 0x28

#define LSM9DS1_M_WHO_AM_I 0x0f
#define LSM9DS1_M_CTRL_REG1 0x20
#define LSM9DS1_M_CTRL_REG2 0x21
#define LSM9DS1_M_CTRL_REG3 0x22
#define LSM9DS1_M_CTRL_REG4 0x23
#define LSM9DS1_M_CTRL_REG5 0x24
#define LSM9DS1_M_OUT_X_L 0x28

#define LSM9DS1_AG_WHO_AM_I_VALUE 0x68
#define LSM9DS1_M_WHO_AM_I_VALUE 0x3d
#define LSM9DS1_AUTO_INCREMENT 0x80

#define LSM9DS1_ACCEL_SCALE_M_S2 0.000598550f
#define LSM9DS1_GYRO_SCALE_RAD_S 0.000152716f
#define LSM9DS1_MAG_SCALE_GAUSS 0.00014f

static const struct i2c_dt_spec ag_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(lsm9ds1));
static const struct i2c_dt_spec mag_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(lsm9ds1_mag));

static int16_t read_le16(const uint8_t *buf) {
    return (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

static bool reg_write(const struct i2c_dt_spec *spec, uint8_t reg, uint8_t value) {
    return i2c_reg_write_byte_dt(spec, reg, value) == 0;
}

static bool reg_read(const struct i2c_dt_spec *spec, uint8_t reg, uint8_t *value) {
    return i2c_reg_read_byte_dt(spec, reg, value) == 0;
}

static bool burst_read(const struct i2c_dt_spec *spec, uint8_t reg, uint8_t *buf, uint8_t len) {
    return i2c_burst_read_dt(spec, reg | LSM9DS1_AUTO_INCREMENT, buf, len) == 0;
}

bool lsm9ds1_local_init() {
    if (!device_is_ready(ag_i2c.bus) || !device_is_ready(mag_i2c.bus)) {
        return false;
    }

    uint8_t who_am_i = 0;
    if (!reg_read(&ag_i2c, LSM9DS1_AG_WHO_AM_I, &who_am_i) ||
        who_am_i != LSM9DS1_AG_WHO_AM_I_VALUE) {
        return false;
    }

    if (!reg_read(&mag_i2c, LSM9DS1_M_WHO_AM_I, &who_am_i) ||
        who_am_i != LSM9DS1_M_WHO_AM_I_VALUE) {
        return false;
    }

    if (!reg_write(&ag_i2c, LSM9DS1_AG_CTRL_REG8, 0x44)) return false;
    k_msleep(10);

    if (!reg_write(&ag_i2c, LSM9DS1_AG_CTRL_REG1_G, 0x60)) return false;
    if (!reg_write(&ag_i2c, LSM9DS1_AG_CTRL_REG4, 0x38)) return false;
    if (!reg_write(&ag_i2c, LSM9DS1_AG_CTRL_REG5_XL, 0x38)) return false;
    if (!reg_write(&ag_i2c, LSM9DS1_AG_CTRL_REG6_XL, 0x60)) return false;

    if (!reg_write(&mag_i2c, LSM9DS1_M_CTRL_REG1, 0xfc)) return false;
    if (!reg_write(&mag_i2c, LSM9DS1_M_CTRL_REG2, 0x00)) return false;
    if (!reg_write(&mag_i2c, LSM9DS1_M_CTRL_REG3, 0x00)) return false;
    if (!reg_write(&mag_i2c, LSM9DS1_M_CTRL_REG4, 0x0c)) return false;
    if (!reg_write(&mag_i2c, LSM9DS1_M_CTRL_REG5, 0x40)) return false;

    return true;
}

bool lsm9ds1_local_read_imu(float *ax, float *ay, float *az, float *gx, float *gy, float *gz) {
    uint8_t buf[6];

    if (!burst_read(&ag_i2c, LSM9DS1_AG_OUT_X_L_XL, buf, sizeof(buf))) {
        return false;
    }
    *ax = read_le16(buf + 0) * LSM9DS1_ACCEL_SCALE_M_S2;
    *ay = read_le16(buf + 2) * LSM9DS1_ACCEL_SCALE_M_S2;
    *az = read_le16(buf + 4) * LSM9DS1_ACCEL_SCALE_M_S2;

    if (!burst_read(&ag_i2c, LSM9DS1_AG_OUT_X_L_G, buf, sizeof(buf))) {
        return false;
    }
    *gx = read_le16(buf + 0) * LSM9DS1_GYRO_SCALE_RAD_S;
    *gy = read_le16(buf + 2) * LSM9DS1_GYRO_SCALE_RAD_S;
    *gz = read_le16(buf + 4) * LSM9DS1_GYRO_SCALE_RAD_S;

    return true;
}

bool lsm9ds1_local_read_mag(float *mx, float *my, float *mz) {
    uint8_t buf[6];

    if (!burst_read(&mag_i2c, LSM9DS1_M_OUT_X_L, buf, sizeof(buf))) {
        return false;
    }

    *mx = read_le16(buf + 0) * LSM9DS1_MAG_SCALE_GAUSS;
    *my = read_le16(buf + 2) * LSM9DS1_MAG_SCALE_GAUSS;
    *mz = read_le16(buf + 4) * LSM9DS1_MAG_SCALE_GAUSS;

    return true;
}

#else

bool lsm9ds1_local_init() {
    return false;
}

bool lsm9ds1_local_read_imu(float *ax, float *ay, float *az, float *gx, float *gy, float *gz) {
    ARG_UNUSED(ax);
    ARG_UNUSED(ay);
    ARG_UNUSED(az);
    ARG_UNUSED(gx);
    ARG_UNUSED(gy);
    ARG_UNUSED(gz);
    return false;
}

bool lsm9ds1_local_read_mag(float *mx, float *my, float *mz) {
    ARG_UNUSED(mx);
    ARG_UNUSED(my);
    ARG_UNUSED(mz);
    return false;
}

#endif
