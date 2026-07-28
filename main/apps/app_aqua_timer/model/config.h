/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "cues.h"
#include <string>

namespace model {

/**
 * @brief Aqua Timer settings, persisted in the NVS namespace "aqua".
 *
 */
struct AquaConfig {
    static constexpr const char* kNamespace = "aqua";

    std::string ssid;
    std::string password;
    /* Folder URL the routine CSVs live under, e.g.
       https://raw.githubusercontent.com/OWNER/REPO/main/routines
       One field rather than owner/repo/branch: fewer things to typo, and it
       works unchanged with Gists, Pages, or a LAN host. */
    std::string baseUrl;
    CueMode cueMode = CueMode::SoundAndBuzz;

    static AquaConfig load();
    void save() const;

    bool isConfigured() const
    {
        return !ssid.empty() && !baseUrl.empty();
    }

    /** @brief Build the URL for a file in the routines folder. */
    std::string urlFor(const std::string& fileName) const;
};

/**
 * @brief Reject anything that is not a plain https URL, and strip a trailing
 * slash so urlFor() never produces a double separator.
 *
 * @return true if the URL was acceptable; `url` is normalised in place.
 */
bool normalizeBaseUrl(std::string& url, std::string& error);

}  // namespace model
