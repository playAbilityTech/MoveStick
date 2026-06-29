#include "apds9960_sensor.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if APDS9960_AVAILABLE

#define APDS9960_REG_ENABLE 0x80
#define APDS9960_REG_ATIME 0x81
#define APDS9960_REG_WTIME 0x83
#define APDS9960_REG_PPULSE 0x8E
#define APDS9960_REG_CONTROL 0x8F
#define APDS9960_REG_CONFIG2 0x90
#define APDS9960_REG_ID 0x92
#define APDS9960_REG_CDATAL 0x94
#define APDS9960_REG_PDATA 0x9C
#define APDS9960_REG_CONFIG3 0x9F
#define APDS9960_REG_GPENTH 0xA0
#define APDS9960_REG_GEXTH 0xA1
#define APDS9960_REG_GCONF1 0xA2
#define APDS9960_REG_GCONF2 0xA3
#define APDS9960_REG_GOFFSET_U 0xA4
#define APDS9960_REG_GOFFSET_D 0xA5
#define APDS9960_REG_GPULSE 0xA6
#define APDS9960_REG_GOFFSET_L 0xA7
#define APDS9960_REG_GOFFSET_R 0xA9
#define APDS9960_REG_GCONF3 0xAA
#define APDS9960_REG_GCONF4 0xAB
#define APDS9960_REG_GFLVL 0xAE
#define APDS9960_REG_GSTATUS 0xAF
#define APDS9960_REG_GFIFO_U 0xFC

#define APDS9960_ID 0xAB
#define APDS9960_ID_ALT 0x9C

#define APDS9960_ENABLE_PON BIT(0)
#define APDS9960_ENABLE_AEN BIT(1)
#define APDS9960_ENABLE_PEN BIT(2)
#define APDS9960_ENABLE_WEN BIT(3)
#define APDS9960_ENABLE_GEN BIT(6)

#define APDS9960_GSTATUS_GVALID BIT(0)
#define APDS9960_GCONF4_GMODE BIT(0)

#define APDS9960_MAX_GESTURE_DATASETS 32
#define APDS9960_GESTURE_DATA_THRESHOLD 0
#define APDS9960_GESTURE_SENSITIVITY 5
#define APDS9960_GESTURE_AXIS_SCALE 327

#define APDS9960_GPENTH_HIGH_SENSITIVITY 5
#define APDS9960_GEXTH_HIGH_SENSITIVITY 2
#define APDS9960_GCONF1_HIGH_SENSITIVITY 0x00
#define APDS9960_GCONF2_HIGH_SENSITIVITY 0x60
#define APDS9960_GPULSE_HIGH_SENSITIVITY 0xFF

typedef struct {
    uint8_t u;
    uint8_t d;
    uint8_t l;
    uint8_t r;
} gesture_sample_t;

typedef struct {
    apds9960_gesture_t direction;
    int16_t x;
    int16_t y;
    uint16_t strength;
} gesture_result_t;

static const struct i2c_dt_spec apds_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(apds9960));

static bool read_reg(uint8_t reg, uint8_t* value) {
    return i2c_reg_read_byte_dt(&apds_i2c, reg, value) == 0;
}

static bool write_reg(uint8_t reg, uint8_t value) {
    return i2c_reg_write_byte_dt(&apds_i2c, reg, value) == 0;
}

static bool read_burst(uint8_t reg, uint8_t* buffer, uint8_t len) {
    return i2c_burst_read_dt(&apds_i2c, reg, buffer, len) == 0;
}

static uint16_t read_u16_le(const uint8_t* buffer) {
    return (uint16_t) buffer[0] | ((uint16_t) buffer[1] << 8);
}

static int gesture_ratio(uint8_t first, uint8_t second) {
    uint16_t sum = first + second;
    if (sum == 0) {
        return 0;
    }
    return (((int) first - (int) second) * 100) / (int) sum;
}

static bool gesture_sample_valid(const gesture_sample_t* sample) {
    return sample->u > APDS9960_GESTURE_DATA_THRESHOLD &&
           sample->d > APDS9960_GESTURE_DATA_THRESHOLD &&
           sample->l > APDS9960_GESTURE_DATA_THRESHOLD &&
           sample->r > APDS9960_GESTURE_DATA_THRESHOLD;
}

static int16_t scale_gesture_delta(int delta) {
    delta = CLAMP(delta, -100, 100);
    return (int16_t)(delta * APDS9960_GESTURE_AXIS_SCALE);
}

static gesture_result_t decode_gesture(const gesture_sample_t* first,
                                       const gesture_sample_t* last) {
    gesture_result_t result = {
        .direction = APDS9960_GESTURE_NONE,
        .x = 0,
        .y = 0,
        .strength = 0
    };
    int ud_delta = gesture_ratio(last->u, last->d) -
                   gesture_ratio(first->u, first->d);
    int lr_delta = gesture_ratio(last->l, last->r) -
                   gesture_ratio(first->l, first->r);
    int strength = MAX(abs(ud_delta), abs(lr_delta));

    int ud_state = 0;
    int lr_state = 0;

    result.x = scale_gesture_delta(lr_delta);
    result.y = scale_gesture_delta(ud_delta);
    result.strength = (uint16_t)MIN(strength, 100) * 255 / 100;

    if (ud_delta >= APDS9960_GESTURE_SENSITIVITY) {
        ud_state = 1;
    } else if (ud_delta <= -APDS9960_GESTURE_SENSITIVITY) {
        ud_state = -1;
    }

    if (lr_delta >= APDS9960_GESTURE_SENSITIVITY) {
        lr_state = 1;
    } else if (lr_delta <= -APDS9960_GESTURE_SENSITIVITY) {
        lr_state = -1;
    }

    if (ud_state == 0 && lr_state == 0) {
        return result;
    }

    if (abs(ud_delta) >= abs(lr_delta)) {
        result.direction = ud_state > 0 ? APDS9960_GESTURE_DOWN : APDS9960_GESTURE_UP;
    } else {
        result.direction = lr_state > 0 ? APDS9960_GESTURE_RIGHT : APDS9960_GESTURE_LEFT;
    }

    return result;
}

static bool read_gesture(apds9960_sensor_data_t* data) {
    uint8_t gstatus = 0;
    uint8_t level = 0;
    bool have_first = false;
    gesture_sample_t first = {};
    gesture_sample_t last = {};

    data->gesture = APDS9960_GESTURE_NONE;
    data->gesture_x = 0;
    data->gesture_y = 0;
    data->gesture_strength = 0;

    if (!read_reg(APDS9960_REG_GSTATUS, &gstatus)) {
        return false;
    }
    if ((gstatus & APDS9960_GSTATUS_GVALID) == 0) {
        return true;
    }
    if (!read_reg(APDS9960_REG_GFLVL, &level)) {
        return false;
    }

    if (level > APDS9960_MAX_GESTURE_DATASETS) {
        level = APDS9960_MAX_GESTURE_DATASETS;
    }

    for (uint8_t i = 0; i < level; i++) {
        gesture_sample_t sample;
        if (!read_burst(APDS9960_REG_GFIFO_U, (uint8_t*) &sample, sizeof(sample))) {
            return false;
        }
        if (!gesture_sample_valid(&sample)) {
            continue;
        }
        if (!have_first) {
            first = sample;
            have_first = true;
        }
        last = sample;
    }

    if (have_first) {
        gesture_result_t result = decode_gesture(&first, &last);
        data->gesture = result.direction;
        data->gesture_x = result.x;
        data->gesture_y = result.y;
        data->gesture_strength = result.strength;
        write_reg(APDS9960_REG_GCONF4, APDS9960_GCONF4_GMODE);
    }

    return true;
}

bool apds9960_sensor_init(void) {
    uint8_t id = 0;
    bool configured = true;

    if (!device_is_ready(apds_i2c.bus)) {
        return false;
    }

    if (!read_reg(APDS9960_REG_ID, &id)) {
        return false;
    }
    if (id != APDS9960_ID && id != APDS9960_ID_ALT) {
        return false;
    }

    if (!write_reg(APDS9960_REG_ENABLE, 0)) {
        return false;
    }

    configured = configured && write_reg(APDS9960_REG_ATIME, 219);
    configured = configured && write_reg(APDS9960_REG_WTIME, 246);
    configured = configured && write_reg(APDS9960_REG_PPULSE, 0x87);
    configured = configured && write_reg(APDS9960_REG_CONTROL, 0x09);
    configured = configured && write_reg(APDS9960_REG_CONFIG2, 0x01);
    configured = configured && write_reg(APDS9960_REG_CONFIG3, 0x00);

    configured = configured && write_reg(APDS9960_REG_GPENTH, APDS9960_GPENTH_HIGH_SENSITIVITY);
    configured = configured && write_reg(APDS9960_REG_GEXTH, APDS9960_GEXTH_HIGH_SENSITIVITY);
    configured = configured && write_reg(APDS9960_REG_GCONF1, APDS9960_GCONF1_HIGH_SENSITIVITY);
    configured = configured && write_reg(APDS9960_REG_GCONF2, APDS9960_GCONF2_HIGH_SENSITIVITY);
    configured = configured && write_reg(APDS9960_REG_GOFFSET_U, 0);
    configured = configured && write_reg(APDS9960_REG_GOFFSET_D, 0);
    configured = configured && write_reg(APDS9960_REG_GOFFSET_L, 0);
    configured = configured && write_reg(APDS9960_REG_GOFFSET_R, 0);
    configured = configured && write_reg(APDS9960_REG_GPULSE, APDS9960_GPULSE_HIGH_SENSITIVITY);
    configured = configured && write_reg(APDS9960_REG_GCONF3, 0x00);
    configured = configured && write_reg(APDS9960_REG_GCONF4, APDS9960_GCONF4_GMODE);
    if (!configured) {
        return false;
    }

    if (!write_reg(APDS9960_REG_ENABLE, APDS9960_ENABLE_PON)) {
        return false;
    }
    k_msleep(10);

    return write_reg(APDS9960_REG_ENABLE,
                     APDS9960_ENABLE_PON |
                     APDS9960_ENABLE_AEN |
                     APDS9960_ENABLE_PEN |
                     APDS9960_ENABLE_WEN |
                     APDS9960_ENABLE_GEN);
}

bool apds9960_sensor_read(apds9960_sensor_data_t* data) {
    uint8_t color_data[8] = {};

    if (data == NULL) {
        return false;
    }

    memset(data, 0, sizeof(*data));

    if (!read_reg(APDS9960_REG_PDATA, &data->proximity)) {
        return false;
    }

    if (read_burst(APDS9960_REG_CDATAL, color_data, sizeof(color_data))) {
        data->clear = read_u16_le(color_data);
        data->red = read_u16_le(color_data + 2);
        data->green = read_u16_le(color_data + 4);
        data->blue = read_u16_le(color_data + 6);
    }

    return read_gesture(data);
}

#else

bool apds9960_sensor_init(void) {
    return false;
}

bool apds9960_sensor_read(apds9960_sensor_data_t* data) {
    ARG_UNUSED(data);
    return false;
}

#endif
