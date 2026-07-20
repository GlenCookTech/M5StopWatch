/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "routine_store.h"
#include "routine_parser.h"

#include <esp_vfs_fat.h>
#include <mooncake_log.h>

#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace model;

static const std::string_view _tag = "aqua-store";

namespace {

constexpr const char* _index_path = "/spiflash/routines/index.csv";
/* Cap on a single cached file, so a corrupt cache entry cannot exhaust heap. */
constexpr size_t _max_file_bytes = 64 * 1024;

std::string routine_path(const std::string& file_name)
{
    return std::string(store::kRoutineDir) + "/" + file_name;
}

/**
 * @brief Reject anything that could escape the routines directory. The index is
 * fetched from the network, so its file names are untrusted input.
 *
 */
bool is_safe_name(const std::string& file_name)
{
    if (file_name.empty() || file_name.size() > 48) return false;
    if (file_name.find('/') != std::string::npos) return false;
    if (file_name.find('\\') != std::string::npos) return false;
    if (file_name.find("..") != std::string::npos) return false;
    return true;
}

std::string read_whole_file(const std::string& path)
{
    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }

    std::string content;
    char buffer[512];
    while (true) {
        const size_t read = fread(buffer, 1, sizeof(buffer), file);
        if (read == 0) break;
        if (content.size() + read > _max_file_bytes) {
            mclog::tagError(_tag, "file too large, truncating: {}", path);
            break;
        }
        content.append(buffer, read);
    }
    fclose(file);
    return content;
}

/**
 * @brief Write via a .tmp sibling then rename, so an interrupted write can never
 * leave a half-written routine in the cache. Mirrors hal_badge.cpp:400-417.
 *
 */
bool write_file_atomic(const std::string& path, const std::string& content)
{
    const std::string temp_path = path + ".tmp";

    FILE* file = fopen(temp_path.c_str(), "wb");
    if (file == nullptr) {
        mclog::tagError(_tag, "failed to open for write: {}", temp_path);
        return false;
    }
    const size_t written = content.empty() ? 0 : fwrite(content.data(), 1, content.size(), file);
    fclose(file);

    if (written != content.size()) {
        mclog::tagError(_tag, "short write: {}", temp_path);
        unlink(temp_path.c_str());
        return false;
    }

    if (unlink(path.c_str()) != 0 && errno != ENOENT) {
        mclog::tagError(_tag, "failed to remove existing: {}", path);
        unlink(temp_path.c_str());
        return false;
    }

    if (rename(temp_path.c_str(), path.c_str()) != 0) {
        mclog::tagError(_tag, "failed to rename into place: {}", path);
        unlink(temp_path.c_str());
        return false;
    }

    return true;
}

}  // namespace

bool store::ensureDir()
{
    if (mkdir(kRoutineDir, 0775) == 0 || errno == EEXIST) {
        return true;
    }
    mclog::tagError(_tag, "failed to create routine dir: {}, errno={}", kRoutineDir, errno);
    return false;
}

uint64_t store::freeBytes()
{
    uint64_t total = 0;
    uint64_t free  = 0;
    // Base path must match the mount point used by wear_levelling_init().
    // esp_vfs_fat_info's third out-param is FREE bytes, not used.
    if (esp_vfs_fat_info("/spiflash", &total, &free) != ESP_OK) {
        mclog::tagError(_tag, "failed to query free space");
        return 0;
    }
    return free;
}

std::vector<RoutineSummary> store::listRoutines()
{
    std::vector<RoutineSummary> summaries;

    const std::string csv = read_whole_file(_index_path);
    if (csv.empty()) {
        return summaries;
    }

    const auto result = parseIndex(csv, summaries);
    if (!result.ok) {
        mclog::tagError(_tag, "cached index is unreadable: {}", result.error);
        return {};
    }
    return summaries;
}

bool store::writeIndex(const std::vector<RoutineSummary>& summaries)
{
    if (!ensureDir()) return false;
    return write_file_atomic(_index_path, buildIndexCsv(summaries));
}

std::string store::readRoutineCsv(const std::string& fileName)
{
    if (!is_safe_name(fileName)) {
        mclog::tagError(_tag, "rejected unsafe file name: {}", fileName);
        return {};
    }
    return read_whole_file(routine_path(fileName));
}

bool store::writeRoutineCsv(const std::string& fileName, const std::string& csv)
{
    if (!is_safe_name(fileName)) {
        mclog::tagError(_tag, "rejected unsafe file name: {}", fileName);
        return false;
    }
    if (!ensureDir()) return false;
    return write_file_atomic(routine_path(fileName), csv);
}

bool store::loadRoutine(const std::string& fileName, Routine& out, std::string& error)
{
    const std::string csv = readRoutineCsv(fileName);
    if (csv.empty()) {
        error = "not in cache";
        return false;
    }

    const auto result = parseRoutine(csv, fileName, out);
    if (!result.ok) {
        error = result.error;
        return false;
    }

    for (const auto& warning : result.warnings) {
        mclog::tagWarn(_tag, "{}: {}", fileName, warning);
    }
    return true;
}
