/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "config.h"
#include <functional>
#include <string>
#include <vector>

namespace model {

struct SyncResult {
    bool ok = false;
    std::string error;                ///< set when the whole sync failed
    size_t downloaded = 0;            ///< routines successfully cached
    size_t total      = 0;            ///< routines listed in the index
    std::vector<std::string> failed;  ///< per-file failures, "name: reason"
};

/**
 * @brief Download index.csv and every routine it lists into the flash cache.
 *
 * Runs the whole network+parse+write sequence on a dedicated FreeRTOS task with
 * a large stack (the TLS handshake needs more than the main task provides), then
 * blocks the caller until it completes. Progress strings are delivered via
 * onProgress from the worker task; the callback is responsible for its own LVGL
 * locking.
 *
 * The local index.csv is written last, so an interrupted or all-failed sync
 * leaves the previous cache — and therefore a working class — intact.
 *
 * @param config Wi-Fi credentials and repo base URL
 * @param onProgress human-readable status lines (may be null)
 * @return SyncResult
 */
SyncResult syncRoutines(const AquaConfig& config, const std::function<void(const std::string&)>& onProgress);

}  // namespace model
