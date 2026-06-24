#include "ble_button_led.h"

#include <hardware/timer.h>
#include <pico/cyw43_arch.h>

static uint64_t activity_off_us = 0;
static bool activity_override = false;

void ble_status_led_set_mode(BleLedMode mode) {
    (void) mode;
}

void ble_status_led_set_blink_count(int count) {
    (void) count;
}

void ble_status_led_activity_flash(void) {
    activity_override = true;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    activity_off_us = time_us_64() + 50000;
}

void ble_button_led_init(void) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
}

void ble_button_led_poll(void) {
    uint64_t now = time_us_64();

    if (activity_override && now >= activity_off_us) {
        activity_override = false;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    }
}
