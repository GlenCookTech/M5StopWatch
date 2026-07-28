/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "routine.h"
#include <string>
#include <vector>

namespace model {

/**
 * @brief Outcome of a parse. Rows can be rejected individually without failing
 * the whole file; `warnings` collects those so the UI can surface them.
 *
 */
struct ParseResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;
};

/**
 * @brief Parse a routine CSV.
 *
 * Format:
 *   `#` lines are metadata (`# title: ...`, `# author: ...`) or plain comments.
 *   One header row `section,label,type,seconds,reps` (reps optional).
 *   type = prep | work | rest | power | cooldown | repeat
 *
 * `repeat` is a directive rather than an interval: it re-emits the preceding
 * contiguous run of rows sharing the same section so that the section plays
 * `reps` times in total.
 *
 * Labels may not contain commas; there is no quoting or escaping.
 *
 * @param csv raw file contents
 * @param fileName stored on the routine for display and cache lookup
 * @param out populated on success; left untouched on failure
 * @return ParseResult
 */
ParseResult parseRoutine(const std::string& csv, const std::string& fileName, Routine& out);

/**
 * @brief Parse the routine index (`index.csv`).
 *
 * Format: `#` comments, then a header row `file,title,minutes`.
 * `minutes` is advisory only — real totals are recomputed once a routine is parsed.
 *
 * @param csv raw file contents
 * @param out populated on success; left untouched on failure
 * @return ParseResult
 */
ParseResult parseIndex(const std::string& csv, std::vector<RoutineSummary>& out);

/**
 * @brief Serialise summaries back to index.csv form, for the local cache index.
 *
 */
std::string buildIndexCsv(const std::vector<RoutineSummary>& summaries);

}  // namespace model
