/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <hal/hal.h>
#include <mooncake_log.h>

static const std::string_view _tag = "HAL";

void Hal::init()
{
    mclog::tagInfo(_tag, "init");

    nvs_init();
    i2c_init();
    pmic_init();
    ioe_init();
    delay(50);
    display_init();
    touchpad_init();
    lvgl_init();
    audio_init();
    imu_init();
    rtc_init();
    button_init();
    fs_init();
}

/* -------------------------------------------------------------------------- */
/*                                     I2C                                    */
/* -------------------------------------------------------------------------- */
#include <i2c_bus.h>

#define I2C_SCL_PIN (gpio_num_t)48
#define I2C_SDA_PIN (gpio_num_t)47

void Hal::i2c_init()
{
    mclog::tagInfo(_tag, "i2c init");

    i2c_config_t conf = {
        .mode          = I2C_MODE_MASTER,
        .sda_io_num    = I2C_SDA_PIN,
        .scl_io_num    = I2C_SCL_PIN,
        .sda_pullup_en = true,
        .scl_pullup_en = true,
        .master =
            {
                .clk_speed = 100000,
            },
        .clk_flags = 0,
    };
    _i2c_bus = i2c_bus_create(I2C_NUM_0, &conf);

    i2c_detect();
}
