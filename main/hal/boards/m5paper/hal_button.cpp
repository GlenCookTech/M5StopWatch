/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <hal/hal.h>
#include <mooncake_log.h>
#include <driver/gpio.h>

static const std::string_view _tag = "hal_button";

/*
 * M5Paper side wheel (pins per M5EPD). GPIO 34-39 are input-only with no
 * internal pulls; the board provides external pull-ups. Mapping: the wheel's
 * two directions act as btnA/btnB (previous/next interval in the Aqua Timer),
 * the center press as btnPwr (pause/resume, hold to end class).
 */
#define M5PAPER_KEY_UP_PIN   (gpio_num_t)37
#define M5PAPER_KEY_PUSH_PIN (gpio_num_t)38
#define M5PAPER_KEY_DOWN_PIN (gpio_num_t)39

void Hal::button_init()
{
    mclog::tagInfo(_tag, "button init");

    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << M5PAPER_KEY_UP_PIN) | (1ULL << M5PAPER_KEY_PUSH_PIN) | (1ULL << M5PAPER_KEY_DOWN_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,  // input-only pins, external pulls on board
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);

    // Long-press threshold for the "hold press to end class" gesture
    btnPwr.setHoldThresh(2000);

    mclog::tagInfo(_tag, "wheel levels at boot: up={} push={} down={}", gpio_get_level(M5PAPER_KEY_UP_PIN),
                   gpio_get_level(M5PAPER_KEY_PUSH_PIN), gpio_get_level(M5PAPER_KEY_DOWN_PIN));
}

void Hal::updateButtonStates()
{
    btnA.setRawState(millis(), !gpio_get_level(M5PAPER_KEY_UP_PIN));
    btnB.setRawState(millis(), !gpio_get_level(M5PAPER_KEY_DOWN_PIN));
    btnPwr.setRawState(millis(), !gpio_get_level(M5PAPER_KEY_PUSH_PIN));
}

void Hal::setButtonConfig(ButtonConfig config, bool saveToSettings)
{
    (void)saveToSettings;
    _btn_config = config;
}

const Hal::ButtonConfig& Hal::getButtonConfig(bool loadFromSettings)
{
    (void)loadFromSettings;
    return _btn_config;
}
