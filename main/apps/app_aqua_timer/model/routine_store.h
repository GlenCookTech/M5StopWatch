/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "routine.h"
#include <cstdint>
#include <string>
#include <vector>

namespace model {

/**
 * @brief The on-flash routine cache at /spiflash/routines.
 *
 * Note the `storage` partition is only 4 MB and is shared with the badge app's
 * images, so callers must check freeBytes() before a sync.
 *
 */
namespace store {

constexpr const char* kRoutineDir = "/spiflash/routines";
/* Refuse to sync below this much free space, so a sync can never brick the
   badge app by filling the shared partition. */
constexpr uint64_t kMinFreeBytes = 512 * 1024;

bool ensureDir();
uint64_t freeBytes();

/** @brief Read the cached index. Empty vector if there is no cache yet. */
std::vector<RoutineSummary> listRoutines();

/** @brief Overwrite the cached index. Written last during a sync, so a partial
 *  sync leaves the previous index — and therefore a working class — intact. */
bool writeIndex(const std::vector<RoutineSummary>& summaries);

/** @brief Read raw CSV for a cached routine. Empty string if absent. */
std::string readRoutineCsv(const std::string& fileName);

/** @brief Atomically write a routine CSV (.tmp then rename). */
bool writeRoutineCsv(const std::string& fileName, const std::string& csv);

/** @brief Load and parse a cached routine. */
bool loadRoutine(const std::string& fileName, Routine& out, std::string& error);

}  // namespace store
}  // namespace model
