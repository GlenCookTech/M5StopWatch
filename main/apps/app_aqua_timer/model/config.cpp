/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "config.h"
#include <hal/utils/settings/settings.h>

#include <vector>

using namespace model;

namespace {

/**
 * @brief Turn a GitHub web URL into the raw.githubusercontent.com equivalent.
 *
 * Pasting the browser address bar is the obvious thing to do, but
 * github.com/OWNER/REPO/tree/REF/PATH serves the rendered HTML file viewer, so
 * the sync fetches ~100 KB of markup and fails on the size cap a long way from
 * the actual mistake. /raw/ only redirects there, costing an extra round trip.
 *
 * @return true if `url` was a GitHub tree/blob/raw URL and has been rewritten.
 */
bool rewriteGithubWebUrl(std::string& url)
{
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return false;
    }

    // A query or fragment (?plain=1, #L1) is part of the web view, never the file
    std::string rest = url.substr(scheme_end + 3);
    rest             = rest.substr(0, rest.find_first_of("?#"));
    while (!rest.empty() && rest.back() == '/') {
        rest.pop_back();
    }

    std::vector<std::string> parts;
    for (size_t begin = 0; begin <= rest.size();) {
        size_t slash = rest.find('/', begin);
        if (slash == std::string::npos) {
            slash = rest.size();
        }
        parts.push_back(rest.substr(begin, slash - begin));
        begin = slash + 1;
    }

    // host / owner / repo / kind / ref [/ path…]
    if (parts.size() < 5) {
        return false;
    }
    std::string host = parts[0];
    if (host.rfind("www.", 0) == 0) {
        host = host.substr(4);
    }
    if (host != "github.com") {
        return false;
    }
    if (parts[3] != "tree" && parts[3] != "blob" && parts[3] != "raw") {
        return false;
    }

    /* A branch name containing a slash is indistinguishable from the path here,
       so the first segment after tree/blob/raw is taken as the ref. Branches
       shared this way are normally plain (main, a tag, a commit SHA); anything
       else can still be pasted as a raw.githubusercontent.com URL directly. */
    std::string rewritten = "https://raw.githubusercontent.com/" + parts[1] + "/" + parts[2] + "/" + parts[4];
    for (size_t i = 5; i < parts.size(); i++) {
        if (!parts[i].empty()) {
            rewritten += "/" + parts[i];
        }
    }

    url = rewritten;
    return true;
}

}  // namespace

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
    // Do this before the length check: the raw form is shorter than the web one
    rewriteGithubWebUrl(url);

    if (url.size() > 255) {
        error = "URL is too long";
        return false;
    }

    return true;
}
