/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "http_get.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>

namespace net {
namespace {

constexpr const char* _tag = "net-http";

/* esp_http_client only follows redirects inside esp_http_client_perform(); the
   open/fetch_headers path used here has to do it by hand, so cap the chain. */
constexpr int _max_redirects = 5;

bool isRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

/**
 * @brief Issue the GET and follow any 3xx, leaving the client positioned on the
 * final response's body.
 *
 * config.disable_auto_redirect has no effect on this path — the redirect check
 * lives in esp_http_client_perform(), which is deliberately not used so the
 * status and length can be vetted before a body byte is buffered. Without this
 * loop a plain `https://github.com/...` or `http://` base URL surfaces as
 * "HTTP 301" instead of fetching.
 */
bool openFollowingRedirects(esp_http_client_handle_t client, int& status, int64_t& contentLength, std::string& error)
{
    for (int redirects = 0;; redirects++) {
        const esp_err_t ret = esp_http_client_open(client, 0);
        if (ret != ESP_OK) {
            error = std::string("connection failed: ") + esp_err_to_name(ret);
            return false;
        }

        contentLength = esp_http_client_fetch_headers(client);
        status        = esp_http_client_get_status_code(client);

        if (!isRedirect(status)) {
            return true;
        }
        if (redirects >= _max_redirects) {
            error = "too many redirects (HTTP " + std::to_string(status) + ")";
            return false;
        }
        // Repoints the client at the Location header; fails only if it is absent
        if (esp_http_client_set_redirection(client) != ESP_OK) {
            error = "HTTP " + std::to_string(status) + " without a Location header";
            return false;
        }
        // Reopen against the new target; the redirect body goes with the socket
        esp_http_client_close(client);
    }
}

bool readBody(esp_http_client_handle_t client, int status, int64_t contentLength, std::string& out, size_t maxBytes,
              std::string& error)
{
    if (status < 200 || status >= 300) {
        error = "HTTP " + std::to_string(status);
        return false;
    }
    if (contentLength > static_cast<int64_t>(maxBytes)) {
        error = "response too large (" + std::to_string(contentLength) + " bytes)";
        return false;
    }

    if (contentLength > 0) {
        out.reserve(static_cast<size_t>(contentLength));
    }

    char buffer[1024];
    while (true) {
        const int read = esp_http_client_read(client, buffer, sizeof(buffer));
        if (read < 0) {
            error = "read error";
            return false;
        }
        if (read == 0) {
            // Complete only when the transfer really finished, not on a stall
            if (esp_http_client_is_complete_data_received(client)) break;
            continue;
        }
        if (out.size() + static_cast<size_t>(read) > maxBytes) {
            error = "response exceeded " + std::to_string(maxBytes) + " bytes";
            return false;
        }
        out.append(buffer, static_cast<size_t>(read));
    }

    return true;
}

}  // namespace

bool httpGet(const std::string& url, std::string& out, size_t maxBytes, std::string& error)
{
    out.clear();

    esp_http_client_config_t config = {};
    config.url                      = url.c_str();
    config.timeout_ms               = 15000;
    config.buffer_size              = 2048;
    config.crt_bundle_attach        = esp_crt_bundle_attach;
    config.disable_auto_redirect    = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        error = "failed to init HTTP client";
        return false;
    }

    int status             = 0;
    int64_t content_length = 0;
    bool ok                = openFollowingRedirects(client, status, content_length, error) &&
                             readBody(client, status, content_length, out, maxBytes, error);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!ok) {
        ESP_LOGE(_tag, "GET %s failed: %s", url.c_str(), error.c_str());
        out.clear();
    }
    return ok;
}

}  // namespace net
