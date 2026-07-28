/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ap_core.h"

#include <cstdio>
#include <mutex>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

namespace net::ap_core {
namespace {

constexpr const char* _tag            = "net-ap-core";
constexpr const char* _ap_ssid_prefix = "M5StopWatch";

}  // namespace

bool ensureWifiStackReady()
{
    static std::mutex mutex;
    static bool initialized = false;

    std::lock_guard<std::mutex> lock(mutex);
    if (initialized) {
        return true;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(_tag, "nvs init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(_tag, "netif init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(_tag, "event loop init failed: %s", esp_err_to_name(ret));
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable         = false;
    ret                    = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(_tag, "wifi init failed: %s", esp_err_to_name(ret));
        return false;
    }

    initialized = true;
    return true;
}

std::string makeApSsid()
{
    uint8_t mac[6] = {};
    esp_err_t ret  = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (ret != ESP_OK) {
        ESP_LOGW(_tag, "read mac failed: %s", esp_err_to_name(ret));
        return std::string(_ap_ssid_prefix);
    }

    char ssid[32] = {};
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X", _ap_ssid_prefix, mac[4], mac[5]);
    return std::string(ssid);
}

}  // namespace net::ap_core
