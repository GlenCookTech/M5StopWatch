/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <string>

namespace net::wifi_sta {

/**
 * @brief Join a Wi-Fi network in station mode, blocking until connected or timed out.
 *
 * Shares the one-time stack init with the AP paths via net::ap_core, and flips
 * the radio to STA mode. Safe to call after the badge captive portal has used
 * AP mode; it stops Wi-Fi and reconfigures.
 *
 * @param ssid network name
 * @param password network password (empty for open networks)
 * @param timeoutMs give up after this long
 * @param error populated on failure
 * @return true once an IP has been acquired
 */
bool connect(const std::string& ssid, const std::string& password, uint32_t timeoutMs, std::string& error);

/**
 * @brief Disconnect and stop the station. Idempotent; always call after connect().
 *
 */
void disconnect();

}  // namespace net::wifi_sta
