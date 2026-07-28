/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <hal/hal.h>
#include <mooncake_log.h>
#include <driver/gpio.h>

static const std::string_view _tag = "HAL";

// M5Paper v1.1 power rails (pin names per M5EPD)
#define M5PAPER_MAIN_PWR_PIN (gpio_num_t)2
#define M5PAPER_EPD_PWR_PIN  (gpio_num_t)23

void Hal::init()
{
    mclog::tagInfo(_tag, "init (M5Paper)");

    // Latch main power before anything else — on battery the board browns out
    // the moment the user releases the power switch otherwise. M5GFX asserts
    // this pin too during autodetect, but that is too late to rely on.
    gpio_reset_pin(M5PAPER_MAIN_PWR_PIN);
    gpio_set_direction(M5PAPER_MAIN_PWR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(M5PAPER_MAIN_PWR_PIN, 1);
    gpio_reset_pin(M5PAPER_EPD_PWR_PIN);
    gpio_set_direction(M5PAPER_EPD_PWR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(M5PAPER_EPD_PWR_PIN, 1);

    nvs_init();
    // No i2c_init: the only I2C peripheral this board uses is the GT911 touch
    // panel, which M5GFX drives itself on I2C_NUM_1 (SDA 21 / SCL 22). The
    // BM8563 RTC and SHT30 sensor on the same bus are unused by this firmware.
    display_init();
    lvgl_init();
    button_init();
    fs_init();
}
