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

}  // namespace

bool httpGet(const std::string& url, std::string& out, size_t maxBytes, std::string& error)
{
    out.clear();

    esp_http_client_config_t config = {};
    config.url                      = url.c_str();
    config.timeout_ms               = 15000;
    config.buffer_size              = 2048;
    config.crt_bundle_attach        = esp_crt_bundle_attach;
    // Follow the 302 that raw.githubusercontent.com / github.io may issue
    config.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        error = "failed to init HTTP client";
        return false;
    }

    bool ok = false;

    // Open / fetch_headers / read explicitly (rather than the event API) so the
    // status and length can be vetted before a single body byte is buffered.
    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        error = std::string("connection failed: ") + esp_err_to_name(ret);
        esp_http_client_cleanup(client);
        return false;
    }

    const int64_t content_length = esp_http_client_fetch_headers(client);
    const int status             = esp_http_client_get_status_code(client);

    if (status < 200 || status >= 300) {
        error = "HTTP " + std::to_string(status);
    } else if (content_length > static_cast<int64_t>(maxBytes)) {
        error = "response too large (" + std::to_string(content_length) + " bytes)";
    } else {
        if (content_length > 0) {
            out.reserve(static_cast<size_t>(content_length));
        }

        char buffer[1024];
        ok = true;
        while (true) {
            const int read = esp_http_client_read(client, buffer, sizeof(buffer));
            if (read < 0) {
                error = "read error";
                ok    = false;
                break;
            }
            if (read == 0) {
                // Complete only when the transfer really finished, not on a stall
                if (esp_http_client_is_complete_data_received(client)) break;
                continue;
            }
            if (out.size() + static_cast<size_t>(read) > maxBytes) {
                error = "response exceeded " + std::to_string(maxBytes) + " bytes";
                ok    = false;
                break;
            }
            out.append(buffer, static_cast<size_t>(read));
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!ok) {
        ESP_LOGE(_tag, "GET %s failed: %s", url.c_str(), error.c_str());
        out.clear();
    }
    return ok;
}

}  // namespace net
