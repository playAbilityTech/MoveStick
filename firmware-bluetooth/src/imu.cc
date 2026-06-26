#include "imu.h"
#include "imu_descriptor.h"
#include "lsm9ds1_local.h"
#include "config.h"
#include "descriptor_parser.h"
#include "globals.h"
#include "platform.h"
#include "remapper.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <math.h>

#if IMU_AVAILABLE

#define IMU_VIRTUAL_INTERFACE 0x1000
#define CALIBRATION_SAMPLES 200

#define IMU_SAMPLE_RATE_MS 15
#define MAX_ERROR_COUNT_BEFORE_BACKOFF 5
#define ERROR_BACKOFF_MULTIPLIER 4
#define CALIBRATION_RETRY_DELAY_MS 500

#define PI 3.14159265359f
#define RAD_TO_DEG (180.0f / PI)
#define GRAVITY 9.81f

#define LED_ACTIVITY_DURATION_MS 50

#define MIN_DT_SECONDS 0.005f
#define MAX_DT_SECONDS 0.050f
#define EXPECTED_DT_SECONDS 0.015f
#define CALIBRATION_SAMPLE_DELAY_MS 5

#define IMU_ODR_FREQUENCY 52
#define ACCEL_SCALE_RANGE 2
#define GYRO_SCALE_RANGE 125

#define MAX_FILTER_BUFFER_SIZE 16
#define MAG_CAL_MIN_RANGE 0.01f
#define MAG_FIELD_MIN_GAUSS 0.05f
#define MAG_FIELD_MAX_GAUSS 2.0f

typedef struct {
    float beta_base;
    float beta_min;
    float beta_max;
    float stationary_threshold;
    float accel_trust_threshold_high;
    float accel_trust_threshold_low;
    float bias_update_rate;
    float gyro_deadzone;
    float angle_clamp_limit;
    float magnitude_filter_alpha;
} imu_config_t;

static imu_config_t imu_config = {
    .beta_base = 0.1f,
    .beta_min = 0.01f,
    .beta_max = 0.3f,
    .stationary_threshold = 0.01f,
    .accel_trust_threshold_high = 2.0f,
    .accel_trust_threshold_low = 0.5f,
    .bias_update_rate = 0.001f,
    .gyro_deadzone = 0.001f,
    .angle_clamp_limit = 45.0f,
    .magnitude_filter_alpha = 0.9f
};

typedef struct {
    float y, alpha;
} iir_t;

typedef struct {
    float buffer[MAX_FILTER_BUFFER_SIZE];
    int index;
    int count;
    int size;
    bool initialized;
} moving_avg_filter_t;

static volatile float madgwick_q0 = 1.0f;
static volatile float madgwick_q1 = 0.0f;
static volatile float madgwick_q2 = 0.0f;
static volatile float madgwick_q3 = 0.0f;

#if IMU_HAS_LSM6DS3TR_C
static const struct device* imu_dev;
#endif
static void imu_work_fn(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(imu_work, imu_work_fn);

static bool has_magnetometer = false;
static volatile float pitch_offset = 0.0f;
static volatile float roll_offset = 0.0f;
static volatile float yaw_offset = 0.0f;
static bool orientation_offset_initialized = false;
static bool yaw_reference_initialized = false;
static int64_t last_timestamp = 0;

static volatile float gyro_bias_x = 0.0f;
static volatile float gyro_bias_y = 0.0f;
static volatile float gyro_bias_z = 0.0f;
static float accel_bias_x = 0.0f;
static float accel_bias_y = 0.0f;
static float accel_bias_z = 0.0f;
static bool is_calibrated = false;

static iir_t magnitude_filter = {.y = 9.81f, .alpha = 0.9f};
static uint32_t error_count = 0;

static uint8_t last_known_angle_clamp_limit = 90;

static moving_avg_filter_t pitch_filter = {
    .index = 0,
    .count = 0,
    .initialized = false
};

static moving_avg_filter_t roll_filter = {
    .index = 0,
    .count = 0,
    .initialized = false
};

static moving_avg_filter_t yaw_filter = {
    .index = 0,
    .count = 0,
    .initialized = false
};

static bool mag_cal_initialized = false;
static float mag_min_x = 0.0f;
static float mag_min_y = 0.0f;
static float mag_min_z = 0.0f;
static float mag_max_x = 0.0f;
static float mag_max_y = 0.0f;
static float mag_max_z = 0.0f;

extern const struct gpio_dt_spec led0;
extern struct k_work_delayable activity_led_off_work;

static float inv_sqrt(float x) {
    float halfx = 0.5f * x;
    float y = x;
    long i = *(long*)&y;
    i = 0x5f3759df - (i >> 1);
    y = *(float*)&i;
    y = y * (1.5f - (halfx * y * y));
    return y;
}

static float compute_dynamic_beta(float hp_magnitude) {
    if (hp_magnitude < imu_config.accel_trust_threshold_low) {
        return imu_config.beta_max;
    } else if (hp_magnitude > imu_config.accel_trust_threshold_high) {
        return imu_config.beta_min;
    } else {
        float ratio = (hp_magnitude - imu_config.accel_trust_threshold_low) / 
                     (imu_config.accel_trust_threshold_high - imu_config.accel_trust_threshold_low);
        return imu_config.beta_max - ratio * (imu_config.beta_max - imu_config.beta_min);
    }
}

static void madgwick_update_imu(float gx, float gy, float gz, float ax, float ay, float az, float dt, float beta) {
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2, _8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

    qDot1 = 0.5f * (-madgwick_q1 * gx - madgwick_q2 * gy - madgwick_q3 * gz);
    qDot2 = 0.5f * (madgwick_q0 * gx + madgwick_q2 * gz - madgwick_q3 * gy);
    qDot3 = 0.5f * (madgwick_q0 * gy - madgwick_q1 * gz + madgwick_q3 * gx);
    qDot4 = 0.5f * (madgwick_q0 * gz + madgwick_q1 * gy - madgwick_q2 * gx);

    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

        recipNorm = inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recipNorm;
        ay *= recipNorm;
        az *= recipNorm;

        _2q0 = 2.0f * madgwick_q0;
        _2q1 = 2.0f * madgwick_q1;
        _2q2 = 2.0f * madgwick_q2;
        _2q3 = 2.0f * madgwick_q3;
        _4q0 = 4.0f * madgwick_q0;
        _4q1 = 4.0f * madgwick_q1;
        _4q2 = 4.0f * madgwick_q2;
        _8q1 = 8.0f * madgwick_q1;
        _8q2 = 8.0f * madgwick_q2;
        q0q0 = madgwick_q0 * madgwick_q0;
        q1q1 = madgwick_q1 * madgwick_q1;
        q2q2 = madgwick_q2 * madgwick_q2;
        q3q3 = madgwick_q3 * madgwick_q3;

        s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
        s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * madgwick_q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
        s2 = 4.0f * q0q0 * madgwick_q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
        s3 = 4.0f * q1q1 * madgwick_q3 - _2q1 * ax + 4.0f * q2q2 * madgwick_q3 - _2q2 * ay;
        float step_norm = s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3;
        if (step_norm > 0.000000001f) {
            recipNorm = inv_sqrt(step_norm);
            s0 *= recipNorm;
            s1 *= recipNorm;
            s2 *= recipNorm;
            s3 *= recipNorm;

            qDot1 -= beta * s0;
            qDot2 -= beta * s1;
            qDot3 -= beta * s2;
            qDot4 -= beta * s3;
        }
    }

    madgwick_q0 += qDot1 * dt;
    madgwick_q1 += qDot2 * dt;
    madgwick_q2 += qDot3 * dt;
    madgwick_q3 += qDot4 * dt;

    recipNorm = inv_sqrt(madgwick_q0 * madgwick_q0 + madgwick_q1 * madgwick_q1 + madgwick_q2 * madgwick_q2 + madgwick_q3 * madgwick_q3);
    madgwick_q0 *= recipNorm;
    madgwick_q1 *= recipNorm;
    madgwick_q2 *= recipNorm;
    madgwick_q3 *= recipNorm;
}

static void madgwick_update_marg(float gx, float gy, float gz,
                                 float ax, float ay, float az,
                                 float mx, float my, float mz,
                                 float dt, float beta) {
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float hx, hy;
    float _2q0mx, _2q0my, _2q0mz, _2q1mx;
    float _2q0, _2q1, _2q2, _2q3, _2q0q2, _2q2q3;
    float q0q0, q0q1, q0q2, q0q3, q1q1, q1q2, q1q3, q2q2, q2q3, q3q3;
    float _2bx, _2bz, _4bx, _4bz;

    if ((mx == 0.0f && my == 0.0f && mz == 0.0f) ||
        (ax == 0.0f && ay == 0.0f && az == 0.0f)) {
        madgwick_update_imu(gx, gy, gz, ax, ay, az, dt, beta);
        return;
    }

    qDot1 = 0.5f * (-madgwick_q1 * gx - madgwick_q2 * gy - madgwick_q3 * gz);
    qDot2 = 0.5f * (madgwick_q0 * gx + madgwick_q2 * gz - madgwick_q3 * gy);
    qDot3 = 0.5f * (madgwick_q0 * gy - madgwick_q1 * gz + madgwick_q3 * gx);
    qDot4 = 0.5f * (madgwick_q0 * gz + madgwick_q1 * gy - madgwick_q2 * gx);

    recipNorm = inv_sqrt(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    recipNorm = inv_sqrt(mx * mx + my * my + mz * mz);
    mx *= recipNorm;
    my *= recipNorm;
    mz *= recipNorm;

    _2q0mx = 2.0f * madgwick_q0 * mx;
    _2q0my = 2.0f * madgwick_q0 * my;
    _2q0mz = 2.0f * madgwick_q0 * mz;
    _2q1mx = 2.0f * madgwick_q1 * mx;
    _2q0 = 2.0f * madgwick_q0;
    _2q1 = 2.0f * madgwick_q1;
    _2q2 = 2.0f * madgwick_q2;
    _2q3 = 2.0f * madgwick_q3;
    _2q0q2 = 2.0f * madgwick_q0 * madgwick_q2;
    _2q2q3 = 2.0f * madgwick_q2 * madgwick_q3;
    q0q0 = madgwick_q0 * madgwick_q0;
    q0q1 = madgwick_q0 * madgwick_q1;
    q0q2 = madgwick_q0 * madgwick_q2;
    q0q3 = madgwick_q0 * madgwick_q3;
    q1q1 = madgwick_q1 * madgwick_q1;
    q1q2 = madgwick_q1 * madgwick_q2;
    q1q3 = madgwick_q1 * madgwick_q3;
    q2q2 = madgwick_q2 * madgwick_q2;
    q2q3 = madgwick_q2 * madgwick_q3;
    q3q3 = madgwick_q3 * madgwick_q3;

    hx = mx * q0q0 - _2q0my * madgwick_q3 + _2q0mz * madgwick_q2 +
         mx * q1q1 + _2q1 * my * madgwick_q2 + _2q1 * mz * madgwick_q3 -
         mx * q2q2 - mx * q3q3;
    hy = _2q0mx * madgwick_q3 + my * q0q0 - _2q0mz * madgwick_q1 +
         _2q1mx * madgwick_q2 - my * q1q1 + my * q2q2 +
         _2q2 * mz * madgwick_q3 - my * q3q3;
    _2bx = sqrtf(hx * hx + hy * hy);
    _2bz = -_2q0mx * madgwick_q2 + _2q0my * madgwick_q1 + mz * q0q0 +
           _2q1mx * madgwick_q3 - mz * q1q1 + _2q2 * my * madgwick_q3 -
           mz * q2q2 + mz * q3q3;
    _4bx = 2.0f * _2bx;
    _4bz = 2.0f * _2bz;

    s0 = -_2q2 * (2.0f * q1q3 - _2q0q2 - ax) +
         _2q1 * (2.0f * q0q1 + _2q2q3 - ay) -
         _2bz * madgwick_q2 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
         (-_2bx * madgwick_q3 + _2bz * madgwick_q1) *
             (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
         _2bx * madgwick_q2 *
             (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
    s1 = _2q3 * (2.0f * q1q3 - _2q0q2 - ax) +
         _2q0 * (2.0f * q0q1 + _2q2q3 - ay) -
         4.0f * madgwick_q1 * (1.0f - 2.0f * q1q1 - 2.0f * q2q2 - az) +
         _2bz * madgwick_q3 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
         (_2bx * madgwick_q2 + _2bz * madgwick_q0) *
             (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
         (_2bx * madgwick_q3 - _4bz * madgwick_q1) *
             (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
    s2 = -_2q0 * (2.0f * q1q3 - _2q0q2 - ax) +
         _2q3 * (2.0f * q0q1 + _2q2q3 - ay) -
         4.0f * madgwick_q2 * (1.0f - 2.0f * q1q1 - 2.0f * q2q2 - az) +
         (-_4bx * madgwick_q2 - _2bz * madgwick_q0) *
             (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
         (_2bx * madgwick_q1 + _2bz * madgwick_q3) *
             (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
         (_2bx * madgwick_q0 - _4bz * madgwick_q2) *
             (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
    s3 = _2q1 * (2.0f * q1q3 - _2q0q2 - ax) +
         _2q2 * (2.0f * q0q1 + _2q2q3 - ay) +
         (-_4bx * madgwick_q3 + _2bz * madgwick_q1) *
             (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
         (-_2bx * madgwick_q0 + _2bz * madgwick_q2) *
             (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
         _2bx * madgwick_q1 *
             (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);

    float step_norm = s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3;
    if (step_norm > 0.000000001f) {
        recipNorm = inv_sqrt(step_norm);
        s0 *= recipNorm;
        s1 *= recipNorm;
        s2 *= recipNorm;
        s3 *= recipNorm;

        qDot1 -= beta * s0;
        qDot2 -= beta * s1;
        qDot3 -= beta * s2;
        qDot4 -= beta * s3;
    }

    madgwick_q0 += qDot1 * dt;
    madgwick_q1 += qDot2 * dt;
    madgwick_q2 += qDot3 * dt;
    madgwick_q3 += qDot4 * dt;

    recipNorm = inv_sqrt(madgwick_q0 * madgwick_q0 + madgwick_q1 * madgwick_q1 +
                         madgwick_q2 * madgwick_q2 + madgwick_q3 * madgwick_q3);
    madgwick_q0 *= recipNorm;
    madgwick_q1 *= recipNorm;
    madgwick_q2 *= recipNorm;
    madgwick_q3 *= recipNorm;
}

static float madgwick_get_pitch(void) {
    return asinf(-2.0f * (madgwick_q1 * madgwick_q3 - madgwick_q0 * madgwick_q2)) * RAD_TO_DEG;
}

static float madgwick_get_roll(void) {
    return atan2f(2.0f * (madgwick_q0 * madgwick_q1 + madgwick_q2 * madgwick_q3), 
                  1.0f - 2.0f * (madgwick_q1 * madgwick_q1 + madgwick_q2 * madgwick_q2)) * RAD_TO_DEG;
}

static float madgwick_get_yaw(void) {
    float yaw = atan2f(2.0f * (madgwick_q0 * madgwick_q3 + madgwick_q1 * madgwick_q2),
                       1.0f - 2.0f * (madgwick_q2 * madgwick_q2 + madgwick_q3 * madgwick_q3)) * RAD_TO_DEG;
    while (yaw > 180.0f) yaw -= 360.0f;
    while (yaw < -180.0f) yaw += 360.0f;
    return yaw;
}

static float wrap_angle_180(float angle) {
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    return angle;
}

static void reset_orientation_filters(void) {
    int adaptive_buffer_size = imu_filter_buffer_size;

    pitch_filter.size = adaptive_buffer_size;
    roll_filter.size = adaptive_buffer_size;
    yaw_filter.size = adaptive_buffer_size;

    pitch_filter.initialized = false;
    roll_filter.initialized = false;
    yaw_filter.initialized = false;

    pitch_filter.count = 0;
    roll_filter.count = 0;
    yaw_filter.count = 0;

    pitch_filter.index = 0;
    roll_filter.index = 0;
    yaw_filter.index = 0;
}

static bool read_imu_raw(float *ax, float *ay, float *az, float *gx, float *gy, float *gz) {
#if IMU_HAS_LSM9DS1_LOCAL
    if (lsm9ds1_local_read_imu(ax, ay, az, gx, gy, gz)) {
        return true;
    }
#endif

#if IMU_HAS_LSM6DS3TR_C
    struct sensor_value accel[3], gyro[3];
    
    if (sensor_sample_fetch(imu_dev) < 0) return false;
    
    if (sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_X, &accel[0]) < 0 ||
        sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Y, &accel[1]) < 0 ||
        sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Z, &accel[2]) < 0) {
        return false;
    }
    
    if (sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_X, &gyro[0]) < 0 ||
        sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_Y, &gyro[1]) < 0 ||
        sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_Z, &gyro[2]) < 0) {
        return false;
    }
    
    *ax = (float)sensor_value_to_double(&accel[0]);
    *ay = (float)sensor_value_to_double(&accel[1]);
    *az = (float)sensor_value_to_double(&accel[2]);
    *gx = (float)sensor_value_to_double(&gyro[0]);
    *gy = (float)sensor_value_to_double(&gyro[1]);
    *gz = (float)sensor_value_to_double(&gyro[2]);
    
    return true;
#else
    return false;
#endif
}

static bool read_mag_raw(float *mx, float *my, float *mz) {
#if IMU_HAS_LSM9DS1_LOCAL
    return lsm9ds1_local_read_mag(mx, my, mz);
#else
    ARG_UNUSED(mx);
    ARG_UNUSED(my);
    ARG_UNUSED(mz);
    return false;
#endif
}

static void update_mag_calibration(float mx, float my, float mz) {
    if (!mag_cal_initialized) {
        mag_min_x = mag_max_x = mx;
        mag_min_y = mag_max_y = my;
        mag_min_z = mag_max_z = mz;
        mag_cal_initialized = true;
        return;
    }

    mag_min_x = fminf(mag_min_x, mx);
    mag_min_y = fminf(mag_min_y, my);
    mag_min_z = fminf(mag_min_z, mz);
    mag_max_x = fmaxf(mag_max_x, mx);
    mag_max_y = fmaxf(mag_max_y, my);
    mag_max_z = fmaxf(mag_max_z, mz);
}

static void apply_mag_calibration(float *mx, float *my, float *mz) {
    if (!mag_cal_initialized) {
        return;
    }

    if ((mag_max_x - mag_min_x) >= MAG_CAL_MIN_RANGE) {
        *mx -= (mag_min_x + mag_max_x) * 0.5f;
    }
    if ((mag_max_y - mag_min_y) >= MAG_CAL_MIN_RANGE) {
        *my -= (mag_min_y + mag_max_y) * 0.5f;
    }
    if ((mag_max_z - mag_min_z) >= MAG_CAL_MIN_RANGE) {
        *mz -= (mag_min_z + mag_max_z) * 0.5f;
    }
}

static bool is_mag_usable(float mx, float my, float mz) {
    float magnitude = sqrtf(mx * mx + my * my + mz * mz);
    return magnitude >= MAG_FIELD_MIN_GAUSS && magnitude <= MAG_FIELD_MAX_GAUSS;
}

static int16_t scale_angle_to_int16(float angle, float min_angle, float max_angle) {
    angle = fmaxf(min_angle, fminf(max_angle, angle));
    float normalized = (angle - min_angle) / (max_angle - min_angle);

    int scaled = (int)((normalized - 0.5f) * 65535.0f);
    return (int16_t)fmaxf(-32768.0f, fminf(32767.0f, (float)scaled));
}

static uint16_t scale_magnitude_to_uint16(float magnitude, float max_magnitude) {
    magnitude = fmaxf(0.0f, fminf(max_magnitude, magnitude));
    float normalized = magnitude / max_magnitude;
    int scaled = (int)(normalized * 255.0f);
    return (uint16_t)fmaxf(0.0f, fminf(255.0f, (float)scaled));
}

static void clamp_angle_to_limit(float* angle) {
    float current_clamp_limit = (float)imu_angle_clamp_limit;
    *angle = fmaxf(-current_clamp_limit, fminf(current_clamp_limit, *angle));
}

static float apply_deadzone(float value, float deadzone) {
    if (fabsf(value) < deadzone) {
        return 0.0f;
    }
    return value > 0 ? value - deadzone : value + deadzone;
}

static void calibrate_orientation(float pitch, float roll, float yaw) {
    pitch_offset = pitch;
    roll_offset = roll;
    yaw_offset = yaw;
    orientation_offset_initialized = true;
}

static bool calibrate_sensors(void) {
    float sum_accel_x = 0.0f, sum_accel_y = 0.0f, sum_accel_z = 0.0f;
    float sum_gyro_x = 0.0f, sum_gyro_y = 0.0f, sum_gyro_z = 0.0f;
    
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
        float ax, ay, az, gx, gy, gz;
        if (!read_imu_raw(&ax, &ay, &az, &gx, &gy, &gz)) {
            return false;
        }
        
        sum_accel_x += ax;
        sum_accel_y += ay;
        sum_accel_z += az;
        sum_gyro_x += gx;
        sum_gyro_y += gy;
        sum_gyro_z += gz;
        
        k_msleep(CALIBRATION_SAMPLE_DELAY_MS);
    }
    
    accel_bias_x = sum_accel_x / CALIBRATION_SAMPLES;
    accel_bias_y = sum_accel_y / CALIBRATION_SAMPLES;
    accel_bias_z = sum_accel_z / CALIBRATION_SAMPLES - GRAVITY;
    
    gyro_bias_x = sum_gyro_x / CALIBRATION_SAMPLES;
    gyro_bias_y = sum_gyro_y / CALIBRATION_SAMPLES;
    gyro_bias_z = sum_gyro_z / CALIBRATION_SAMPLES;
    
    return true;
}

static float iir_update_magnitude(iir_t *filter, float input) {
    filter->y = filter->alpha * filter->y + (1.0f - filter->alpha) * input;
    return filter->y;
}

static float moving_avg_filter_update(moving_avg_filter_t *filter, float input) {
    int bufsize = filter->size;
    if (bufsize < 1) bufsize = 1;
    if (bufsize > MAX_FILTER_BUFFER_SIZE) bufsize = MAX_FILTER_BUFFER_SIZE;
    filter->buffer[filter->index] = input;
    filter->index = (filter->index + 1) % bufsize;
    
    if (!filter->initialized) {
        filter->initialized = true;
        filter->count = 1;
        return input;
    }
    
    if (filter->count < bufsize) {
        filter->count++;
    }
    
    float sum = 0.0f;
    for (int i = 0; i < filter->count; i++) {
        sum += filter->buffer[i];
    }
    
    return sum / filter->count;
}

static void update_gyro_bias_if_stationary(float gx_raw, float gy_raw, float gz_raw, float hp_magnitude) {
    if (hp_magnitude < imu_config.stationary_threshold) {
        gyro_bias_x += imu_config.bias_update_rate * (gx_raw - gyro_bias_x);
        gyro_bias_y += imu_config.bias_update_rate * (gy_raw - gyro_bias_y);
        gyro_bias_z += imu_config.bias_update_rate * (gz_raw - gyro_bias_z);
    }
}

static void imu_work_fn(struct k_work* work) {
#if IMU_HAS_LSM6DS3TR_C
    if (!imu_dev) {
        error_count++;
        if (error_count > MAX_ERROR_COUNT_BEFORE_BACKOFF) {
    
            k_work_reschedule(&imu_work, K_MSEC(IMU_SAMPLE_RATE_MS * ERROR_BACKOFF_MULTIPLIER));
            error_count = 0;
        } else {
            k_work_reschedule(&imu_work, K_MSEC(IMU_SAMPLE_RATE_MS));
        }
        return;
    }
#endif

    int64_t now = k_uptime_get();
    float dt = last_timestamp ? (now - last_timestamp) / 1000.0f : EXPECTED_DT_SECONDS;
    last_timestamp = now;


    if (dt < MIN_DT_SECONDS || dt > MAX_DT_SECONDS) {
        dt = EXPECTED_DT_SECONDS;
    }

    if (!is_calibrated) {
        if (calibrate_sensors()) {
            is_calibrated = true;
            magnitude_filter.alpha = imu_config.magnitude_filter_alpha;
            orientation_offset_initialized = false;
        } else {
            error_count++;

            k_work_reschedule(&imu_work, K_MSEC(CALIBRATION_RETRY_DELAY_MS));
            return;
        }
    }

    float ax_raw, ay_raw, az_raw, gx_raw, gy_raw, gz_raw;
    if (!read_imu_raw(&ax_raw, &ay_raw, &az_raw, &gx_raw, &gy_raw, &gz_raw)) {
        error_count++;
        gpio_pin_set_dt(&led0, false);
        

        uint32_t delay_ms = (error_count > MAX_ERROR_COUNT_BEFORE_BACKOFF) ? 
                           IMU_SAMPLE_RATE_MS * ERROR_BACKOFF_MULTIPLIER : 
                           IMU_SAMPLE_RATE_MS;
        k_work_reschedule(&imu_work, K_MSEC(delay_ms));
        return;
    }

    float mx_raw = 0.0f, my_raw = 0.0f, mz_raw = 0.0f;
    float mx = 0.0f, my = 0.0f, mz = 0.0f;
    bool mag_usable = false;
    if (has_magnetometer && !read_mag_raw(&mx_raw, &my_raw, &mz_raw)) {
        error_count++;
        gpio_pin_set_dt(&led0, false);

        uint32_t delay_ms = (error_count > MAX_ERROR_COUNT_BEFORE_BACKOFF) ?
                           IMU_SAMPLE_RATE_MS * ERROR_BACKOFF_MULTIPLIER :
                           IMU_SAMPLE_RATE_MS;
        k_work_reschedule(&imu_work, K_MSEC(delay_ms));
        return;
    }

    if (has_magnetometer) {
        update_mag_calibration(mx_raw, my_raw, mz_raw);
        mx = mx_raw;
        my = my_raw;
        mz = mz_raw;
        apply_mag_calibration(&mx, &my, &mz);
        mag_usable = is_mag_usable(mx, my, mz);
    }
    
    error_count = 0;
    
    if (last_known_angle_clamp_limit != imu_angle_clamp_limit) {
        last_known_angle_clamp_limit = imu_angle_clamp_limit;
        imu_config.angle_clamp_limit = (float)imu_angle_clamp_limit;
        reset_orientation_filters();
    }
    
    float ax = ax_raw - accel_bias_x;
    float ay = ay_raw - accel_bias_y;
    float az = az_raw - accel_bias_z;
    float gx = gx_raw - gyro_bias_x;
    float gy = gy_raw - gyro_bias_y;
    float gz = gz_raw - gyro_bias_z;
    
    gx = apply_deadzone(gx, imu_config.gyro_deadzone);
    gy = apply_deadzone(gy, imu_config.gyro_deadzone);
    gz = apply_deadzone(gz, imu_config.gyro_deadzone);
    
    float accel_mag = sqrtf(ax * ax + ay * ay + az * az);
    float filtered_mag = iir_update_magnitude(&magnitude_filter, accel_mag);
    float hp_magnitude = accel_mag - filtered_mag;
    
    update_gyro_bias_if_stationary(gx_raw, gy_raw, gz_raw, fabsf(hp_magnitude));
    
    float beta = compute_dynamic_beta(fabsf(hp_magnitude));
    
    if (mag_usable) {
        madgwick_update_marg(gx, gy, gz, ax, ay, az, mx, my, mz, dt, beta);
        yaw_reference_initialized = true;
    } else {
        madgwick_update_imu(gx, gy, gz, ax, ay, az, dt, beta);
    }
    
    float pitch = madgwick_get_pitch();
    float roll = madgwick_get_roll();
    bool yaw_valid = has_magnetometer && yaw_reference_initialized;
    float yaw = yaw_valid ? madgwick_get_yaw() : 0.0f;

    if (!orientation_offset_initialized && (!has_magnetometer || yaw_valid)) {
        calibrate_orientation(pitch, roll, yaw_valid ? yaw : 0.0f);
    }

    if (!orientation_offset_initialized) {
        k_work_reschedule(&imu_work, K_MSEC(IMU_SAMPLE_RATE_MS));
        return;
    }
    
    float pitch_corrected = -(pitch - pitch_offset);
    float roll_corrected = roll - roll_offset;
    float yaw_corrected = yaw_valid ? wrap_angle_180(yaw - yaw_offset) : 0.0f;
    
    if (imu_pitch_inverted) {
        pitch_corrected = -pitch_corrected;
    }
    if (imu_roll_inverted) {
        roll_corrected = -roll_corrected;
    }
    if (imu_yaw_inverted) {
        yaw_corrected = -yaw_corrected;
    }
    
    pitch_corrected = moving_avg_filter_update(&pitch_filter, pitch_corrected);
    roll_corrected = moving_avg_filter_update(&roll_filter, roll_corrected);
    yaw_corrected = moving_avg_filter_update(&yaw_filter, yaw_corrected);
    
    clamp_angle_to_limit(&yaw_corrected);
    clamp_angle_to_limit(&pitch_corrected);
    clamp_angle_to_limit(&roll_corrected);
    
    float current_clamp_limit = (float)imu_angle_clamp_limit;
    int16_t yaw_scaled = scale_angle_to_int16(yaw_corrected, -current_clamp_limit, current_clamp_limit);
    int16_t pitch_scaled = scale_angle_to_int16(pitch_corrected, -current_clamp_limit, current_clamp_limit);
    int16_t roll_scaled = scale_angle_to_int16(roll_corrected, -current_clamp_limit, current_clamp_limit);
    uint16_t magnitude_scaled = scale_magnitude_to_uint16(hp_magnitude, 25.0f);
    
    if (has_magnetometer) {
        imu_report_9dof_t imu_report = {
            .yaw = yaw_scaled,
            .pitch = pitch_scaled,
            .roll = roll_scaled,
            .magnitude = magnitude_scaled
        };
        handle_received_report((uint8_t*)&imu_report, (int)sizeof(imu_report), IMU_VIRTUAL_INTERFACE);
    } else {
        imu_report_6dof_t imu_report = {
            .pitch = pitch_scaled,
            .roll = roll_scaled,
            .magnitude = magnitude_scaled
        };
        handle_received_report((uint8_t*)&imu_report, (int)sizeof(imu_report), IMU_VIRTUAL_INTERFACE);
    }

    k_work_cancel_delayable(&activity_led_off_work);
    gpio_pin_set_dt(&led0, true);
    k_work_reschedule(&activity_led_off_work, K_MSEC(LED_ACTIVITY_DURATION_MS));


    k_work_reschedule(&imu_work, K_MSEC(IMU_SAMPLE_RATE_MS));
}

bool imu_init() {
#if IMU_HAS_LSM9DS1_LOCAL
    if (!lsm9ds1_local_init()) {
        return false;
    }
    has_magnetometer = true;
#elif IMU_HAS_LSM6DS3TR_C
    imu_dev = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3tr_c));
    
    if (!device_is_ready(imu_dev)) {
        return false;
    }
    has_magnetometer = false;
#else
    return false;
#endif
    
    imu_config.angle_clamp_limit = (float)imu_angle_clamp_limit;
    orientation_offset_initialized = false;
    yaw_reference_initialized = false;
    reset_orientation_filters();

#if IMU_HAS_LSM6DS3TR_C
    struct sensor_value odr_attr;
    odr_attr.val1 = IMU_ODR_FREQUENCY;
    odr_attr.val2 = 0;

    if (sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ,
                        SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr) < 0) {
        return false;
    }

    if (sensor_attr_set(imu_dev, SENSOR_CHAN_GYRO_XYZ,
                        SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr) < 0) {
        return false;
    }

    struct sensor_value accel_scale_attr;
    accel_scale_attr.val1 = ACCEL_SCALE_RANGE;
    accel_scale_attr.val2 = 0;

    sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ,
                     SENSOR_ATTR_FULL_SCALE, &accel_scale_attr);

    struct sensor_value angular_scale_attr;
    angular_scale_attr.val1 = GYRO_SCALE_RANGE;
    angular_scale_attr.val2 = 0;

    sensor_attr_set(imu_dev, SENSOR_CHAN_GYRO_XYZ,
                     SENSOR_ATTR_FULL_SCALE, &angular_scale_attr);
#endif

    float ax, ay, az, gx, gy, gz;
    if (!read_imu_raw(&ax, &ay, &az, &gx, &gy, &gz)) {
        return false;
    }

    if (has_magnetometer) {
        parse_descriptor(0x0F0D, 0x00C1, imu_hid_report_desc_9dof, IMU_HID_REPORT_DESC_9DOF_SIZE, IMU_VIRTUAL_INTERFACE, 0);
    } else {
        parse_descriptor(0x0F0D, 0x00C1, imu_hid_report_desc_6dof, IMU_HID_REPORT_DESC_6DOF_SIZE, IMU_VIRTUAL_INTERFACE, 0);
    }
    device_connected_callback(IMU_VIRTUAL_INTERFACE, 0x0F0D, 0x00C1, 0);
    
    their_descriptor_updated = true;


    k_work_schedule(&imu_work, K_MSEC(IMU_SAMPLE_RATE_MS));

    return true;
}

void imu_recalibrate_orientation() {
    if (is_calibrated) {
        orientation_offset_initialized = false;
        reset_orientation_filters();

    }
}

void imu_recalibrate_sensors() {
    if (is_calibrated) {
        is_calibrated = false;
        error_count = 0;
        
        madgwick_q0 = 1.0f;
        madgwick_q1 = 0.0f;
        madgwick_q2 = 0.0f;
        madgwick_q3 = 0.0f;
        orientation_offset_initialized = false;
        yaw_reference_initialized = false;
        mag_cal_initialized = false;
        
        magnitude_filter = (iir_t){.y = 9.81f, .alpha = imu_config.magnitude_filter_alpha};
        reset_orientation_filters();

    }
}

#else

bool imu_init() {
    return true;
}

#endif
