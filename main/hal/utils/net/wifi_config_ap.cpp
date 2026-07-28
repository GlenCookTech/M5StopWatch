/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "wifi_config_ap.h"
#include "ap_core.h"

#include <cstring>
#include <memory>

#include <dns_server.h>
#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <lwip/ip_addr.h>

namespace net::wifi_config_ap {
namespace {

constexpr const char* _tag                = "wifi-config-ap";
constexpr const char* _ap_url             = "http://192.168.4.1";
constexpr EventBits_t _exit_requested_bit = BIT0;
constexpr size_t _max_form_bytes          = 4096;

extern const char aqua_config_ap_html_start[] asm("_binary_aqua_config_ap_html_start");
extern const char aqua_config_ap_html_end[] asm("_binary_aqua_config_ap_html_end");

constexpr const char* _captive_portal_urls[] = {
    "/hotspot-detect.html",       "/generate_204*", "/mobile/status.php",
    "/check_network_status.txt",  "/ncsi.txt",      "/fwlink/",
    "/connectivity-check.html",   "/success.txt",   "/portal.html",
    "/library/test/success.html",
};

/* Decode application/x-www-form-urlencoded, so passwords and URLs survive '+',
   '%20', '%2F' and friends intact. */
std::string url_decode(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '+') {
            out.push_back(' ');
        } else if (in[i] == '%' && i + 2 < in.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(in[i + 1]);
            const int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(in[i]);
            }
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::string form_field(const std::string& body, const std::string& key)
{
    const std::string needle = key + "=";
    size_t pos               = 0;
    while (pos < body.size()) {
        size_t amp       = body.find('&', pos);
        size_t end       = amp == std::string::npos ? body.size() : amp;
        std::string pair = body.substr(pos, end - pos);
        if (pair.rfind(needle, 0) == 0) {
            return url_decode(pair.substr(needle.size()));
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
}

class Session {
public:
    Session(const std::function<void(std::string_view)>& onLog, const SaveHandler& onSave)
        : _on_log(onLog), _on_save(onSave)
    {
    }

    bool run()
    {
        if (!ap_core::ensureWifiStackReady()) {
            log("Wi-Fi initialization failed");
            return false;
        }

        _event_group = xEventGroupCreate();
        if (_event_group == nullptr) {
            log("Failed to create event group");
            return false;
        }

        if (!start_access_point() || !start_web_server()) {
            stop();
            return false;
        }

        log("Connect to Wi-Fi:\n" + _ssid + "\nThen open:\n" + std::string(_ap_url));

        xEventGroupWaitBits(_event_group, _exit_requested_bit, pdTRUE, pdFALSE, portMAX_DELAY);

        stop();
        log("Wi-Fi setup closed");
        return true;
    }

private:
    void log(const std::string& message) const
    {
        ESP_LOGI(_tag, "%s", message.c_str());
        if (_on_log) _on_log(message);
    }

    bool start_access_point()
    {
        static esp_netif_t* ap_netif = nullptr;
        if (ap_netif == nullptr) {
            ap_netif = esp_netif_create_default_wifi_ap();
        }
        if (ap_netif == nullptr) {
            log("Failed to create AP interface");
            return false;
        }

        esp_netif_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
        IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);

        _dns_server = std::make_unique<DnsServer>();
        _dns_server->Start(ip_info.gw);

        _ssid = ap_core::makeApSsid();

        wifi_config_t wifi_config = {};
        strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), _ssid.c_str(), sizeof(wifi_config.ap.ssid) - 1);
        wifi_config.ap.ssid_len       = _ssid.size();
        wifi_config.ap.max_connection = 4;
        wifi_config.ap.authmode       = WIFI_AUTH_OPEN;

        esp_wifi_stop();
        if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &wifi_config) != ESP_OK) {
            log("Failed to configure AP");
            return false;
        }
        esp_wifi_set_ps(WIFI_PS_NONE);
        if (esp_wifi_start() != ESP_OK) {
            log("Failed to start AP");
            return false;
        }
        return true;
    }

    bool start_web_server()
    {
        httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
        config.max_uri_handlers  = 20;
        config.recv_wait_timeout = 15;
        config.send_wait_timeout = 15;
        config.uri_match_fn      = httpd_uri_match_wildcard;

        if (httpd_start(&_server, &config) != ESP_OK) {
            log("Failed to start web server");
            return false;
        }

        httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = &Session::handle_index, .user_ctx = this};
        httpd_uri_t save  = {.uri = "/save", .method = HTTP_POST, .handler = &Session::handle_save, .user_ctx = this};
        httpd_uri_t close = {.uri = "/close", .method = HTTP_POST, .handler = &Session::handle_close, .user_ctx = this};
        httpd_uri_t captive = {
            .uri = nullptr, .method = HTTP_GET, .handler = &Session::handle_captive_portal, .user_ctx = this};

        esp_err_t ret = httpd_register_uri_handler(_server, &index);
        if (ret == ESP_OK) ret = httpd_register_uri_handler(_server, &save);
        if (ret == ESP_OK) ret = httpd_register_uri_handler(_server, &close);
        if (ret == ESP_OK) {
            for (const auto* url : _captive_portal_urls) {
                captive.uri = url;
                ret         = httpd_register_uri_handler(_server, &captive);
                if (ret != ESP_OK) break;
            }
        }
        if (ret != ESP_OK) {
            log("Failed to register routes");
            return false;
        }
        return true;
    }

    void stop()
    {
        if (_server != nullptr) {
            httpd_stop(_server);
            _server = nullptr;
        }
        if (_dns_server) {
            _dns_server->Stop();
            _dns_server.reset();
        }
        esp_wifi_stop();
        if (_event_group != nullptr) {
            vEventGroupDelete(_event_group);
            _event_group = nullptr;
        }
    }

    static Session* self_from_request(httpd_req_t* req)
    {
        return static_cast<Session*>(req->user_ctx);
    }

    static esp_err_t handle_index(httpd_req_t* req)
    {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_send(req, aqua_config_ap_html_start, aqua_config_ap_html_end - aqua_config_ap_html_start);
        return ESP_OK;
    }

    static esp_err_t handle_save(httpd_req_t* req)
    {
        auto* self = self_from_request(req);
        if (self == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no session");
            return ESP_FAIL;
        }

        if (req->content_len <= 0 || static_cast<size_t>(req->content_len) > _max_form_bytes) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad form size");
            return ESP_FAIL;
        }

        std::string body(static_cast<size_t>(req->content_len), '\0');
        size_t offset = 0;
        while (offset < body.size()) {
            const int received = httpd_req_recv(req, body.data() + offset, body.size() - offset);
            if (received <= 0) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "read failed");
                return ESP_FAIL;
            }
            offset += static_cast<size_t>(received);
        }

        Credentials creds;
        creds.ssid     = form_field(body, "ssid");
        creds.password = form_field(body, "password");
        creds.baseUrl  = form_field(body, "base_url");

        std::string message = "Saved";
        bool ok             = self->_on_save ? self->_on_save(creds, message) : false;

        const std::string json =
            std::string("{\"status\":\"") + (ok ? "ok" : "error") + "\",\"message\":\"" + message + "\"}";
        httpd_resp_set_type(req, "application/json");
        if (!ok) httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, json.c_str(), json.size());
        return ESP_OK;
    }

    static esp_err_t handle_close(httpd_req_t* req)
    {
        auto* self = self_from_request(req);
        if (self != nullptr && self->_event_group != nullptr) {
            xEventGroupSetBits(self->_event_group, _exit_requested_bit);
        }
        httpd_resp_sendstr(req, "closing");
        return ESP_OK;
    }

    static esp_err_t handle_captive_portal(httpd_req_t* req)
    {
        const std::string url = std::string(_ap_url) + "/?_=" + std::to_string(esp_timer_get_time());
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", url.c_str());
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }

    httpd_handle_t _server          = nullptr;
    EventGroupHandle_t _event_group = nullptr;
    std::function<void(std::string_view)> _on_log;
    SaveHandler _on_save;
    std::string _ssid;
    std::unique_ptr<DnsServer> _dns_server;
};

}  // namespace

bool run(const std::function<void(std::string_view)>& onLog, const SaveHandler& onSave)
{
    return Session(onLog, onSave).run();
}

}  // namespace net::wifi_config_ap
