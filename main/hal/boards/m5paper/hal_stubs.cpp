/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <hal/hal.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <mooncake_log.h>
#include <algorithm>

/*
 * M5Paper v1.1 has no speaker, no buzzer, no vibration motor, no PMIC and no
 * IMU. Everything the shared app/HAL code can call lands here as a safe no-op;
 * getSpeakerVolume() returning 0 short-circuits every audio:: helper before it
 * generates a single sample. Cues are rendered visually by the e-ink views.
 */

/* ---------------------------------- Audio --------------------------------- */

void Hal::setSpeakerVolume(int volume, bool saveToSettings)
{
    (void)volume;
    (void)saveToSettings;
}

int Hal::getSpeakerVolume(bool loadFromSettings)
{
    (void)loadFromSettings;
    return 0;
}

int Hal::getAudioSampleRate()
{
    return 16000;
}

void Hal::audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain)
{
    (void)durationMs;
    (void)gain;
    data.clear();
}

void Hal::audioPlay(std::vector<int16_t>& data, bool async)
{
    (void)data;
    (void)async;
}

void Hal::updateAudioSpectrum()
{
}

void Hal::playBootSfx()
{
}

/* ----------------------------- Vibrator Motor ----------------------------- */

void Hal::vibrate(uint16_t durationMs, uint8_t strength)
{
    (void)durationMs;
    (void)strength;
}

void Hal::stopVibrate()
{
}

/* ----------------------------------- IMU ---------------------------------- */

void Hal::updateImuData()
{
}

/* ---------------------------------- Power --------------------------------- */

namespace {

// Battery voltage divider on GPIO35 (ADC1 ch7): reading is half the cell voltage
constexpr float _bat_divider   = 2.0f;
constexpr int _bat_empty_mv    = 3300;
constexpr int _bat_full_mv     = 4200;
adc_oneshot_unit_handle_t _adc = nullptr;
adc_cali_handle_t _adc_cali    = nullptr;

void bat_adc_init()
{
    if (_adc != nullptr) {
        return;
    }
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id                     = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&unit_cfg, &_adc) != ESP_OK) {
        _adc = nullptr;
        return;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(_adc, ADC_CHANNEL_7, &chan_cfg);

    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &_adc_cali) != ESP_OK) {
        _adc_cali = nullptr;
    }
}

}  // namespace

uint8_t Hal::getBatteryLevel()
{
    bat_adc_init();
    if (_adc == nullptr) {
        return 100;
    }

    int raw = 0;
    if (adc_oneshot_read(_adc, ADC_CHANNEL_7, &raw) != ESP_OK) {
        return 100;
    }

    int reading_mv = 0;
    if (_adc_cali != nullptr && adc_cali_raw_to_voltage(_adc_cali, raw, &reading_mv) == ESP_OK) {
        // calibrated path
    } else {
        reading_mv = raw * 3300 / 4095;
    }

    const int bat_mv  = (int)(reading_mv * _bat_divider);
    const int percent = (bat_mv - _bat_empty_mv) * 100 / (_bat_full_mv - _bat_empty_mv);
    return (uint8_t)std::clamp(percent, 0, 100);
}

bool Hal::isBatteryCharging(bool strict)
{
    (void)strict;
    return false;
}
