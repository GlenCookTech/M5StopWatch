/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <string>

namespace net::ap_core {

/**
 * @brief Bring up NVS, netif, the default event loop and esp_wifi exactly once.
 *
 * Every Wi-Fi user in the firmware — the badge captive portal, the Aqua Timer
 * config portal, and the STA sync path — MUST route through this one
 * initialiser. Initialising the stack twice returns ESP_ERR_INVALID_STATE and
 * leaves Wi-Fi wedged, so this is a process-wide singleton guarded by a mutex.
 *
 * @return true if the stack is ready.
 */
bool ensureWifiStackReady();

/**
 * @brief SoftAP SSID for this device, e.g. "M5StopWatch-1A2B" (last MAC bytes).
 *
 */
std::string makeApSsid();

}  // namespace net::ap_core
