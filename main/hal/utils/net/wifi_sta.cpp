/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "wifi_sta.h"
#include "ap_core.h"

#include <cstring>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

namespace net::wifi_sta {
namespace {

constexpr const char* _tag        = "net-sta";
constexpr EventBits_t _got_ip_bit = BIT0;
constexpr EventBits_t _fail_bit   = BIT1;
/* Give up on the AP after a handful of association attempts rather than looping. */
constexpr int _max_retries = 5;

EventGroupHandle_t _event_group        = nullptr;
esp_event_handler_instance_t _wifi_any = nullptr;
esp_event_handler_instance_t _ip_got   = nullptr;
int _retry_count                       = 0;

void event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (_retry_count < _max_retries) {
            _retry_count++;
            esp_wifi_connect();
        } else if (_event_group != nullptr) {
            xEventGroupSetBits(_event_group, _fail_bit);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        _retry_count = 0;
        if (_event_group != nullptr) {
            xEventGroupSetBits(_event_group, _got_ip_bit);
        }
    }
}

esp_netif_t* sta_netif()
{
    // Created once and never destroyed, mirroring the AP netif lifecycle in
    // config_ap.cpp. Destroying and recreating netifs is where INVALID_STATE lives.
    static esp_netif_t* netif = nullptr;
    if (netif == nullptr) {
        netif = esp_netif_create_default_wifi_sta();
    }
    return netif;
}

}  // namespace

bool connect(const std::string& ssid, const std::string& password, uint32_t timeoutMs, std::string& error)
{
    if (ssid.empty()) {
        error = "no SSID configured";
        return false;
    }

    if (!ap_core::ensureWifiStackReady()) {
        error = "Wi-Fi init failed";
        return false;
    }

    if (sta_netif() == nullptr) {
        error = "failed to create station interface";
        return false;
    }

    _retry_count = 0;
    _event_group = xEventGroupCreate();
    if (_event_group == nullptr) {
        error = "out of memory";
        return false;
    }

    esp_err_t ret =
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, nullptr, &_wifi_any);
    if (ret == ESP_OK) {
        ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr, &_ip_got);
    }
    if (ret != ESP_OK) {
        error = "failed to register Wi-Fi events";
        disconnect();
        return false;
    }

    // Leave whatever mode the badge portal may have left us in
    esp_wifi_stop();

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password.c_str(), sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK) {
        error = "failed to configure station";
        disconnect();
        return false;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (esp_wifi_start() != ESP_OK) {
        error = "failed to start Wi-Fi";
        disconnect();
        return false;
    }

    const EventBits_t bits =
        xEventGroupWaitBits(_event_group, _got_ip_bit | _fail_bit, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeoutMs));

    if (bits & _got_ip_bit) {
        ESP_LOGI(_tag, "connected to %s", ssid.c_str());
        return true;
    }

    error = (bits & _fail_bit) ? "wrong password or network unavailable" : "connection timed out";
    disconnect();
    return false;
}

void disconnect()
{
    if (_wifi_any != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, _wifi_any);
        _wifi_any = nullptr;
    }
    if (_ip_got != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, _ip_got);
        _ip_got = nullptr;
    }

    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED && ret != ESP_ERR_WIFI_MODE) {
        ESP_LOGW(_tag, "wifi stop failed: %s", esp_err_to_name(ret));
    }

    if (_event_group != nullptr) {
        vEventGroupDelete(_event_group);
        _event_group = nullptr;
    }
    _retry_count = 0;
}

}  // namespace net::wifi_sta
