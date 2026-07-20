/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <string>

namespace net {

/**
 * @brief Fetch a URL into a string over HTTP or HTTPS.
 *
 * HTTPS uses the mbedTLS certificate bundle (esp_crt_bundle_attach), so any
 * public CA — including the one behind raw.githubusercontent.com — is trusted
 * without pinning. The response is rejected before any body is read if the
 * status is not 2xx or the Content-Length exceeds maxBytes.
 *
 * @param url absolute http(s) URL
 * @param out response body on success
 * @param maxBytes hard cap; a larger response is an error, not a truncation
 * @param error populated on failure
 * @return true on a 2xx response fully read
 */
bool httpGet(const std::string& url, std::string& out, size_t maxBytes, std::string& error);

}  // namespace net
