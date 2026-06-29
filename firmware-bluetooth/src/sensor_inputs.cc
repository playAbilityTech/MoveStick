#include "sensor_inputs.h"
#include "activity_led.h"
#include "apds9960_sensor.h"
#include "imu_descriptor.h"
#include "mic_level.h"
#include "motion_fusion.h"
#include "motion_sensor.h"
#include "config.h"
#include "descriptor_parser.h"
#include "globals.h"
#include "platform.h"
#include "remapper.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <math.h>

#if SENSOR_INPUTS_AVAILABLE

#define IMU_VIRTUAL_INTERFACE 0x1000
#define CALIBRATION_SAMPLES 200

#define IMU_SAMPLE_RATE_MS 15
#define MAX_ERROR_COUNT_BEFORE_BACKOFF 5
#define ERROR_BACKOFF_MULTIPLIER 4
#define CALIBRATION_RETRY_DELAY_MS 500

#define GRAVITY 9.81f

#define LED_ACTIVITY_DURATION_MS 50

#define MIN_DT_SECONDS 0.005f
#define MAX_DT_SECONDS 0.050f
#define EXPECTED_DT_SECONDS 0.015f
#define CALIBRATION_SAMPLE_DELAY_MS 5

#define MAX_FILTER_BUFFER_SIZE 16
#define DEFAULT_IMU_FILTER_BUFFER_SIZE 10

#define APDS_GESTURE_LATCH_MS 120
#define APDS_PROXIMITY_NEAR_ON 80
#define APDS_PROXIMITY_NEAR_OFF 60
#define APDS_PROXIMITY_COVERED_ON 180
#define APDS_PROXIMITY_COVERED_OFF 150
#define APDS_AMBIENT_SCALE_SHIFT 6

#define APDS_BUTTON_GESTURE_UP BIT(0)
#define APDS_BUTTON_GESTURE_DOWN BIT(1)
#define APDS_BUTTON_GESTURE_LEFT BIT(2)
#define APDS_BUTTON_GESTURE_RIGHT BIT(3)
#define APDS_BUTTON_NEAR BIT(4)
#define APDS_BUTTON_COVERED BIT(5)

typedef struct {
    float beta_base;
    float beta_min;
    float beta_max;
    float stationary_threshold;
    float accel_trust_threshold_high;
    float accel_trust_threshold_low;
    float bias_update_rate;
    float gyro_deadzone;
    float magnitude_filter_alpha;
} imu_runtime_config_t;

static imu_runtime_config_t imu_config = {
    .beta_base = 0.1f,
    .beta_min = 0.01f,
    .beta_max = 0.3f,
    .stationary_threshold = 0.01f,
    .accel_trust_threshold_high = 2.0f,
    .accel_trust_threshold_low = 0.5f,
    .bias_update_rate = 0.001f,
    .gyro_deadzone = 0.001f,
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

static void imu_work_fn(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(imu_work, imu_work_fn);

bool submit_report(uint16_t interface, uint8_t external_report_id,
                   const uint8_t* payload, uint8_t payload_len);

static bool imu_active = false;
static bool apds_active = false;
static bool has_magnetometer = false;
static bool imu_paused = false;
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

static uint8_t last_known_filter_buffer_size = DEFAULT_IMU_FILTER_BUFFER_SIZE;

static int64_t apds_gesture_latch_until[4] = {};
static int64_t apds_gesture_axis_latch_until = 0;
static int16_t apds_latched_gesture_x = 0;
static int16_t apds_latched_gesture_y = 0;
static uint16_t apds_latched_gesture_strength = 0;
static bool apds_near = false;
static bool apds_covered = false;

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

static void reset_orientation_filters(void) {
    int adaptive_buffer_size = imu_filter_buffer_size;
    if (adaptive_buffer_size < 1) {
        adaptive_buffer_size = 1;
    }
    if (adaptive_buffer_size > MAX_FILTER_BUFFER_SIZE) {
        adaptive_buffer_size = MAX_FILTER_BUFFER_SIZE;
    }

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

static void refresh_filter_config(void) {
    uint8_t current_filter_buffer_size = imu_filter_buffer_size;
    if (current_filter_buffer_size < 1) {
        current_filter_buffer_size = 1;
    }
    if (current_filter_buffer_size > MAX_FILTER_BUFFER_SIZE) {
        current_filter_buffer_size = MAX_FILTER_BUFFER_SIZE;
    }

    if (last_known_filter_buffer_size != current_filter_buffer_size) {
        last_known_filter_buffer_size = current_filter_buffer_size;
        reset_orientation_filters();
    }
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

static uint16_t scale_apds_ambient(uint16_t clear) {
    return MIN(clear >> APDS_AMBIENT_SCALE_SHIFT, 255);
}

static void latch_apds_gesture(apds9960_gesture_t gesture, int64_t now) {
    if (gesture == APDS9960_GESTURE_NONE) {
        return;
    }

    int index = (int)gesture - 1;
    if (index >= 0 && index < 4) {
        apds_gesture_latch_until[index] = now + APDS_GESTURE_LATCH_MS;
    }
}

static void latch_apds_gesture_axes(const apds9960_sensor_data_t* data, int64_t now) {
    if (data->gesture_strength == 0) {
        return;
    }

    apds_latched_gesture_x = data->gesture_x;
    apds_latched_gesture_y = data->gesture_y;
    apds_latched_gesture_strength = data->gesture_strength;
    apds_gesture_axis_latch_until = now + APDS_GESTURE_LATCH_MS;
}

static void get_apds_latched_gesture_axes(int64_t now, int16_t* gesture_x,
                                          int16_t* gesture_y, uint16_t* gesture_strength) {
    if (now < apds_gesture_axis_latch_until) {
        *gesture_x = apds_latched_gesture_x;
        *gesture_y = apds_latched_gesture_y;
        *gesture_strength = apds_latched_gesture_strength;
        return;
    }

    *gesture_x = 0;
    *gesture_y = 0;
    *gesture_strength = 0;
}

static uint8_t apds_latched_gesture_buttons(int64_t now) {
    uint8_t buttons = 0;

    if (now < apds_gesture_latch_until[APDS9960_GESTURE_UP - 1]) {
        buttons |= APDS_BUTTON_GESTURE_UP;
    }
    if (now < apds_gesture_latch_until[APDS9960_GESTURE_DOWN - 1]) {
        buttons |= APDS_BUTTON_GESTURE_DOWN;
    }
    if (now < apds_gesture_latch_until[APDS9960_GESTURE_LEFT - 1]) {
        buttons |= APDS_BUTTON_GESTURE_LEFT;
    }
    if (now < apds_gesture_latch_until[APDS9960_GESTURE_RIGHT - 1]) {
        buttons |= APDS_BUTTON_GESTURE_RIGHT;
    }

    return buttons;
}

static void update_apds_inputs(int64_t now, uint16_t* proximity,
                               uint16_t* ambient_light, int16_t* gesture_x,
                               int16_t* gesture_y, uint16_t* gesture_strength,
                               uint8_t* buttons) {
    *proximity = 0;
    *ambient_light = 0;
    *gesture_x = 0;
    *gesture_y = 0;
    *gesture_strength = 0;
    *buttons = 0;

    if (!apds_active) {
        return;
    }

    apds9960_sensor_data_t apds_data;
    if (apds9960_sensor_read(&apds_data)) {
        *proximity = apds_data.proximity;
        *ambient_light = scale_apds_ambient(apds_data.clear);
        latch_apds_gesture(apds_data.gesture, now);
        latch_apds_gesture_axes(&apds_data, now);

        if (!apds_near && apds_data.proximity >= APDS_PROXIMITY_NEAR_ON) {
            apds_near = true;
        } else if (apds_near && apds_data.proximity <= APDS_PROXIMITY_NEAR_OFF) {
            apds_near = false;
        }

        if (!apds_covered && apds_data.proximity >= APDS_PROXIMITY_COVERED_ON) {
            apds_covered = true;
        } else if (apds_covered && apds_data.proximity <= APDS_PROXIMITY_COVERED_OFF) {
            apds_covered = false;
        }
    }

    get_apds_latched_gesture_axes(now, gesture_x, gesture_y, gesture_strength);

    *buttons = apds_latched_gesture_buttons(now);
    if (apds_near) {
        *buttons |= APDS_BUTTON_NEAR;
    }
    if (apds_covered) {
        *buttons |= APDS_BUTTON_COVERED;
    }
}

static bool submit_sensor_report(const void* report, size_t len) {
    if (len > UINT8_MAX) {
        return false;
    }

    return submit_report(IMU_VIRTUAL_INTERFACE, 0, (const uint8_t*) report, (uint8_t) len);
}

static uint8_t sanitize_angle_limit(uint8_t value) {
    if (value < 1) {
        return 1;
    }
    if (value > 90) {
        return 90;
    }
    return value;
}

static uint8_t sanitize_deadzone(uint8_t value) {
    if (value > 90) {
        return 90;
    }
    return value;
}

static void apply_angle_deadzone(float* angle, uint8_t deadzone_setting) {
    float deadzone = (float)sanitize_deadzone(deadzone_setting);
    if (deadzone <= 0.0f) {
        return;
    }

    if (fabsf(*angle) < deadzone) {
        *angle = 0.0f;
    }
}

static void apply_yaw_deadzone(float* yaw) {
    apply_angle_deadzone(yaw, imu_yaw_deadzone);
}

static float clamp_angle_to_limits(float angle, uint8_t negative_limit, uint8_t positive_limit) {
    float min_angle = -(float)sanitize_angle_limit(negative_limit);
    float max_angle = (float)sanitize_angle_limit(positive_limit);
    return fmaxf(min_angle, fminf(max_angle, angle));
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
        if (!motion_sensor_read_imu(&ax, &ay, &az, &gx, &gy, &gz)) {
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
    int64_t now = k_uptime_get();
    uint16_t apds_proximity = 0;
    uint16_t apds_ambient_light = 0;
    int16_t apds_gesture_x = 0;
    int16_t apds_gesture_y = 0;
    uint16_t apds_gesture_strength = 0;
    uint8_t apds_buttons = 0;

    update_apds_inputs(now, &apds_proximity, &apds_ambient_light,
                       &apds_gesture_x, &apds_gesture_y,
                       &apds_gesture_strength, &apds_buttons);

    if (!imu_active) {
        apds9960_report_t apds_report = {
            .proximity = apds_proximity,
            .ambient_light = apds_ambient_light,
            .gesture_x = apds_gesture_x,
            .gesture_y = apds_gesture_y,
            .gesture_strength = apds_gesture_strength,
            .apds_buttons = apds_buttons
        };
        if (submit_sensor_report(&apds_report, sizeof(apds_report))) {
            activity_led_flash(LED_ACTIVITY_DURATION_MS);
        }
        k_work_reschedule(&imu_work, K_MSEC(IMU_SAMPLE_RATE_MS));
        return;
    }

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
            mic_level_init();
        } else {
            error_count++;

            k_work_reschedule(&imu_work, K_MSEC(CALIBRATION_RETRY_DELAY_MS));
            return;
        }
    }

    float ax_raw, ay_raw, az_raw, gx_raw, gy_raw, gz_raw;
    if (!motion_sensor_read_imu(&ax_raw, &ay_raw, &az_raw, &gx_raw, &gy_raw, &gz_raw)) {
        error_count++;
        activity_led_off();
        

        uint32_t delay_ms = (error_count > MAX_ERROR_COUNT_BEFORE_BACKOFF) ? 
                           IMU_SAMPLE_RATE_MS * ERROR_BACKOFF_MULTIPLIER : 
                           IMU_SAMPLE_RATE_MS;
        k_work_reschedule(&imu_work, K_MSEC(delay_ms));
        return;
    }

    float mx_raw = 0.0f, my_raw = 0.0f, mz_raw = 0.0f;
    float mx = 0.0f, my = 0.0f, mz = 0.0f;
    bool mag_usable = false;
    if (has_magnetometer && !motion_sensor_read_mag(&mx_raw, &my_raw, &mz_raw)) {
        error_count++;
        activity_led_off();

        uint32_t delay_ms = (error_count > MAX_ERROR_COUNT_BEFORE_BACKOFF) ?
                           IMU_SAMPLE_RATE_MS * ERROR_BACKOFF_MULTIPLIER :
                           IMU_SAMPLE_RATE_MS;
        k_work_reschedule(&imu_work, K_MSEC(delay_ms));
        return;
    }

    if (has_magnetometer) {
        motion_fusion_update_mag_calibration(mx_raw, my_raw, mz_raw);
        mx = mx_raw;
        my = my_raw;
        mz = mz_raw;
        motion_fusion_apply_mag_calibration(&mx, &my, &mz);
        mag_usable = motion_fusion_is_mag_usable(mx, my, mz);
    }
    
    error_count = 0;
    
    refresh_filter_config();
    
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
        motion_fusion_update_marg(gx, gy, gz, ax, ay, az, mx, my, mz, dt, beta);
        yaw_reference_initialized = true;
    } else {
        motion_fusion_update_imu(gx, gy, gz, ax, ay, az, dt, beta);
    }
    
    float pitch = motion_fusion_get_pitch();
    float roll = motion_fusion_get_roll();
    bool yaw_valid = has_magnetometer && yaw_reference_initialized;
    float yaw = yaw_valid ? motion_fusion_get_yaw() : 0.0f;

    if (!orientation_offset_initialized && (!has_magnetometer || yaw_valid)) {
        calibrate_orientation(pitch, roll, yaw_valid ? yaw : 0.0f);
    }

    if (!orientation_offset_initialized) {
        k_work_reschedule(&imu_work, K_MSEC(IMU_SAMPLE_RATE_MS));
        return;
    }
    
    float pitch_corrected = -(pitch - pitch_offset);
    float roll_corrected = roll - roll_offset;
    float yaw_corrected = yaw_valid ? motion_fusion_wrap_angle_180(yaw - yaw_offset) : 0.0f;
    
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
    
    apply_angle_deadzone(&pitch_corrected, imu_pitch_deadzone);
    apply_angle_deadzone(&roll_corrected, imu_roll_deadzone);
    apply_yaw_deadzone(&yaw_corrected);
    
    yaw_corrected = clamp_angle_to_limits(yaw_corrected, imu_yaw_neg_max_angle, imu_yaw_pos_max_angle);
    pitch_corrected = clamp_angle_to_limits(pitch_corrected, imu_pitch_neg_max_angle, imu_pitch_pos_max_angle);
    roll_corrected = clamp_angle_to_limits(roll_corrected, imu_roll_neg_max_angle, imu_roll_pos_max_angle);

    float yaw_neg_max = (float)sanitize_angle_limit(imu_yaw_neg_max_angle);
    float yaw_pos_max = (float)sanitize_angle_limit(imu_yaw_pos_max_angle);
    float pitch_neg_max = (float)sanitize_angle_limit(imu_pitch_neg_max_angle);
    float pitch_pos_max = (float)sanitize_angle_limit(imu_pitch_pos_max_angle);
    float roll_neg_max = (float)sanitize_angle_limit(imu_roll_neg_max_angle);
    float roll_pos_max = (float)sanitize_angle_limit(imu_roll_pos_max_angle);
    int16_t yaw_scaled = scale_angle_to_int16(yaw_corrected, -yaw_neg_max, yaw_pos_max);
    int16_t pitch_scaled = scale_angle_to_int16(pitch_corrected, -pitch_neg_max, pitch_pos_max);
    int16_t roll_scaled = scale_angle_to_int16(roll_corrected, -roll_neg_max, roll_pos_max);
    uint16_t magnitude_scaled = scale_magnitude_to_uint16(hp_magnitude, 25.0f);
    uint16_t mic_level = mic_level_get();

    if (imu_paused) {
        yaw_scaled = 0;
        pitch_scaled = 0;
        roll_scaled = 0;
        magnitude_scaled = 0;
        mic_level = 0;
    }
    
    if (has_magnetometer) {
        if (apds_active) {
            imu_report_9dof_apds_t imu_report = {
                .yaw = yaw_scaled,
                .pitch = pitch_scaled,
                .roll = roll_scaled,
                .magnitude = magnitude_scaled,
                .mic_level = mic_level,
                .proximity = apds_proximity,
                .ambient_light = apds_ambient_light,
                .gesture_x = apds_gesture_x,
                .gesture_y = apds_gesture_y,
                .gesture_strength = apds_gesture_strength,
                .apds_buttons = apds_buttons
            };
            submit_sensor_report(&imu_report, sizeof(imu_report));
        } else {
            imu_report_9dof_t imu_report = {
                .yaw = yaw_scaled,
                .pitch = pitch_scaled,
                .roll = roll_scaled,
                .magnitude = magnitude_scaled,
                .mic_level = mic_level
            };
            submit_sensor_report(&imu_report, sizeof(imu_report));
        }
    } else {
        if (apds_active) {
            imu_report_6dof_apds_t imu_report = {
                .pitch = pitch_scaled,
                .roll = roll_scaled,
                .magnitude = magnitude_scaled,
                .mic_level = mic_level,
                .proximity = apds_proximity,
                .ambient_light = apds_ambient_light,
                .gesture_x = apds_gesture_x,
                .gesture_y = apds_gesture_y,
                .gesture_strength = apds_gesture_strength,
                .apds_buttons = apds_buttons
            };
            submit_sensor_report(&imu_report, sizeof(imu_report));
        } else {
            imu_report_6dof_t imu_report = {
                .pitch = pitch_scaled,
                .roll = roll_scaled,
                .magnitude = magnitude_scaled,
                .mic_level = mic_level
            };
            submit_sensor_report(&imu_report, sizeof(imu_report));
        }
    }

    activity_led_flash(LED_ACTIVITY_DURATION_MS);


    k_work_reschedule(&imu_work, K_MSEC(IMU_SAMPLE_RATE_MS));
}

bool sensor_inputs_init() {
    imu_active = false;
    apds_active = false;
    has_magnetometer = false;
    is_calibrated = false;
    error_count = 0;
    imu_paused = false;
    for (int i = 0; i < 4; i++) {
        apds_gesture_latch_until[i] = 0;
    }
    apds_gesture_axis_latch_until = 0;
    apds_latched_gesture_x = 0;
    apds_latched_gesture_y = 0;
    apds_latched_gesture_strength = 0;
    apds_near = false;
    apds_covered = false;

    if (IMU_AVAILABLE && motion_sensor_init(&has_magnetometer)) {
        float ax, ay, az, gx, gy, gz;
        imu_active = motion_sensor_read_imu(&ax, &ay, &az, &gx, &gy, &gz);
    }

    if (APDS9960_AVAILABLE) {
        apds_active = apds9960_sensor_init();
    }

    if (!imu_active && !apds_active) {
        return false;
    }

    if (imu_active) {
        orientation_offset_initialized = false;
        yaw_reference_initialized = false;
        motion_fusion_reset();
        motion_fusion_reset_mag_calibration();
        reset_orientation_filters();
        last_known_filter_buffer_size = pitch_filter.size;
    }

    if (imu_active && has_magnetometer && apds_active) {
        parse_descriptor(0x0F0D, 0x00C1, imu_hid_report_desc_9dof_apds, IMU_HID_REPORT_DESC_9DOF_APDS_SIZE, IMU_VIRTUAL_INTERFACE, 0);
    } else if (imu_active && has_magnetometer) {
        parse_descriptor(0x0F0D, 0x00C1, imu_hid_report_desc_9dof, IMU_HID_REPORT_DESC_9DOF_SIZE, IMU_VIRTUAL_INTERFACE, 0);
    } else if (imu_active && apds_active) {
        parse_descriptor(0x0F0D, 0x00C1, imu_hid_report_desc_6dof_apds, IMU_HID_REPORT_DESC_6DOF_APDS_SIZE, IMU_VIRTUAL_INTERFACE, 0);
    } else if (imu_active) {
        parse_descriptor(0x0F0D, 0x00C1, imu_hid_report_desc_6dof, IMU_HID_REPORT_DESC_6DOF_SIZE, IMU_VIRTUAL_INTERFACE, 0);
    } else {
        parse_descriptor(0x0F0D, 0x00C1, apds9960_hid_report_desc, APDS9960_HID_REPORT_DESC_SIZE, IMU_VIRTUAL_INTERFACE, 0);
    }

    device_connected_callback(IMU_VIRTUAL_INTERFACE, 0x0F0D, 0x00C1, 0);
    
    their_descriptor_updated = true;


    k_work_schedule(&imu_work, K_MSEC(IMU_SAMPLE_RATE_MS));

    return true;
}

void sensor_inputs_recalibrate_orientation() {
    if (imu_active && is_calibrated) {
        orientation_offset_initialized = false;
        reset_orientation_filters();

    }
}

void sensor_inputs_recalibrate_sensors() {
    if (imu_active && is_calibrated) {
        is_calibrated = false;
        error_count = 0;
        
        motion_fusion_reset();
        orientation_offset_initialized = false;
        yaw_reference_initialized = false;
        motion_fusion_reset_mag_calibration();
        
        magnitude_filter = (iir_t){.y = 9.81f, .alpha = imu_config.magnitude_filter_alpha};
        mic_level_reset();
        reset_orientation_filters();

    }
}

void sensor_inputs_pause_imu() {
    if (imu_active) {
        imu_paused = true;
    }
}

void sensor_inputs_resume_imu() {
    imu_paused = false;
}

#else

bool sensor_inputs_init() {
    return true;
}

void sensor_inputs_recalibrate_orientation() {
}

void sensor_inputs_recalibrate_sensors() {
}

void sensor_inputs_pause_imu() {
}

void sensor_inputs_resume_imu() {
}

#endif
