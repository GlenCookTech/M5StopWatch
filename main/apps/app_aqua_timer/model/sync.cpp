/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "sync.h"
#include "routine_parser.h"
#include "routine_store.h"

#include <hal/utils/net/http_get.h>
#include <hal/utils/net/wifi_sta.h>
#include <mooncake_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

using namespace model;

static const std::string_view _tag = "aqua-sync";

namespace {

constexpr uint32_t _connect_timeout_ms = 20000;
constexpr size_t _index_max_bytes      = 32 * 1024;
constexpr size_t _routine_max_bytes    = 64 * 1024;
/* The mbedTLS handshake alone wants ~10-12 KB; give the whole download task room. */
constexpr uint32_t _task_stack_bytes = 12 * 1024;

struct SyncContext {
    const AquaConfig* config                             = nullptr;
    const std::function<void(const std::string&)>* onLog = nullptr;
    SyncResult result;
    SemaphoreHandle_t done = nullptr;
};

void report(SyncContext* ctx, const std::string& message)
{
    mclog::tagInfo(_tag, "{}", message);
    if (ctx->onLog && *ctx->onLog) {
        (*ctx->onLog)(message);
    }
}

void do_sync(SyncContext* ctx)
{
    auto& result       = ctx->result;
    const auto& config = *ctx->config;

    if (!config.isConfigured()) {
        result.error = "Wi-Fi not set up";
        return;
    }

    report(ctx, "Joining " + config.ssid + "…");
    std::string error;
    if (!net::wifi_sta::connect(config.ssid, config.password, _connect_timeout_ms, error)) {
        result.error = "Cannot join " + config.ssid + ": " + error;
        return;
    }

    // From here on, always fall through to disconnect at the end
    do {
        report(ctx, "Fetching routine list…");
        std::string index_csv;
        if (!net::httpGet(config.urlFor("index.csv"), index_csv, _index_max_bytes, error)) {
            result.error = "index.csv: " + error;
            break;
        }

        std::vector<RoutineSummary> summaries;
        const auto index_result = parseIndex(index_csv, summaries);
        if (!index_result.ok) {
            result.error = "index.csv malformed: " + index_result.error;
            break;
        }
        result.total = summaries.size();

        if (!store::ensureDir()) {
            result.error = "Cannot open storage";
            break;
        }
        if (store::freeBytes() < store::kMinFreeBytes) {
            result.error = "Storage full — clear badge images";
            break;
        }

        std::vector<RoutineSummary> cached;
        for (auto& summary : summaries) {
            report(ctx, "Downloading " + summary.title + "…");

            std::string csv;
            if (!net::httpGet(config.urlFor(summary.file), csv, _routine_max_bytes, error)) {
                result.failed.push_back(summary.file + ": " + error);
                continue;
            }

            // Validate before writing, so a corrupt download never enters the cache
            Routine routine;
            const auto parse_result = parseRoutine(csv, summary.file, routine);
            if (!parse_result.ok) {
                result.failed.push_back(summary.file + ": " + parse_result.error);
                continue;
            }

            if (!store::writeRoutineCsv(summary.file, csv)) {
                result.failed.push_back(summary.file + ": write failed");
                continue;
            }

            // Trust the parsed totals over the index's advisory minutes
            summary.title         = routine.title.empty() ? summary.title : routine.title;
            summary.totalSeconds  = routine.totalSeconds;
            summary.intervalCount = routine.intervals.size();
            cached.push_back(summary);
            result.downloaded++;
        }

        if (cached.empty()) {
            result.error = "No routines could be downloaded";
            break;
        }

        // Written last: this is the commit point for the whole sync
        if (!store::writeIndex(cached)) {
            result.error = "Failed to save routine index";
            break;
        }

        result.ok = true;
    } while (false);

    net::wifi_sta::disconnect();
}

void sync_task(void* arg)
{
    auto* ctx = static_cast<SyncContext*>(arg);
    do_sync(ctx);
    xSemaphoreGive(ctx->done);
    vTaskDelete(nullptr);
}

}  // namespace

SyncResult model::syncRoutines(const AquaConfig& config, const std::function<void(const std::string&)>& onProgress)
{
    SyncContext ctx;
    ctx.config = &config;
    ctx.onLog  = &onProgress;
    ctx.done   = xSemaphoreCreateBinary();

    if (ctx.done == nullptr) {
        SyncResult result;
        result.error = "out of memory";
        return result;
    }

    TaskHandle_t task = nullptr;
    const BaseType_t created =
        xTaskCreate(sync_task, "aqua_sync", _task_stack_bytes, &ctx, tskIDLE_PRIORITY + 3, &task);
    if (created != pdPASS) {
        vSemaphoreDelete(ctx.done);
        SyncResult result;
        result.error = "could not start sync task";
        return result;
    }

    // Block the caller until the worker finishes. The worker owns the TLS stack.
    xSemaphoreTake(ctx.done, portMAX_DELAY);
    vSemaphoreDelete(ctx.done);

    return ctx.result;
}
