/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "config.h"
#include <hal/utils/settings/settings.h>

using namespace model;

AquaConfig AquaConfig::load()
{
    AquaConfig config;

    Settings settings(std::string(kNamespace), false);
    config.ssid     = settings.GetString("ssid");
    config.password = settings.GetString("pass");
    config.baseUrl  = settings.GetString("base_url");
    config.cueMode  = settings.GetInt("cue_mode", 0) == 1 ? CueMode::BuzzOnly : CueMode::SoundAndBuzz;

    return config;
}

void AquaConfig::save() const
{
    Settings settings(std::string(kNamespace), true);
    settings.SetString("ssid", ssid);
    settings.SetString("pass", password);
    settings.SetString("base_url", baseUrl);
    settings.SetInt("cue_mode", cueMode == CueMode::BuzzOnly ? 1 : 0);
}

std::string AquaConfig::urlFor(const std::string& fileName) const
{
    return baseUrl + "/" + fileName;
}

bool model::normalizeBaseUrl(std::string& url, std::string& error)
{
    // Trim surrounding whitespace, which is easy to paste in by accident
    const auto not_space = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; };
    size_t begin         = 0;
    while (begin < url.size() && !not_space(url[begin])) begin++;
    size_t end = url.size();
    while (end > begin && !not_space(url[end - 1])) end--;
    url = url.substr(begin, end - begin);

    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    if (url.empty()) {
        error = "URL is empty";
        return false;
    }
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) {
        error = "URL must start with https:// or http://";
        return false;
    }
    if (url.size() > 255) {
        error = "URL is too long";
        return false;
    }

    return true;
}
