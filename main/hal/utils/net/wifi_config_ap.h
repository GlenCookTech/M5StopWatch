/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <functional>
#include <string>
#include <string_view>

namespace net::wifi_config_ap {

/**
 * @brief Credentials captured from the setup form.
 *
 */
struct Credentials {
    std::string ssid;
    std::string password;
    std::string baseUrl;
};

/**
 * @brief Called when the user submits the form. Return false with a message to
 * reject (e.g. a malformed URL) and keep the portal open.
 *
 */
using SaveHandler = std::function<bool(const Credentials&, std::string& message)>;

/**
 * @brief Bring up a SoftAP + captive portal presenting a Wi-Fi / repo setup form.
 *
 * Blocks until the user taps "Done" in the browser (POST /close), mirroring the
 * badge config-AP flow. Shares Wi-Fi stack init with net::ap_core.
 *
 * @param onLog progress/status text for the on-device loading page
 * @param onSave persists submitted credentials
 * @return true if the portal ran and exited cleanly
 */
bool run(const std::function<void(std::string_view)>& onLog, const SaveHandler& onSave);

}  // namespace net::wifi_config_ap
