/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "routine_parser.h"
#include <algorithm>
#include <cctype>

using namespace model;

/* -------------------------------------------------------------------------- */
/*                                   Helpers                                  */
/* -------------------------------------------------------------------------- */

static void trim(std::string& s)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
}

static std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

/**
 * @brief Split a line into fields. Deliberately naive: no quoting, no escaping.
 * Field count is checked by the caller, so a stray comma in a label becomes a
 * loud rejection rather than a silently mangled row.
 *
 */
static std::vector<std::string> splitFields(const std::string& line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        size_t comma = line.find(',', start);
        if (comma == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    for (auto& f : fields) {
        trim(f);
    }
    return fields;
}

/**
 * @brief Split into lines, tolerating CRLF and a UTF-8 BOM.
 *
 */
static std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    size_t begin = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        begin = 3;
    }
    for (size_t i = begin; i < text.size(); i++) {
        if (text[i] == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (text[i] != '\r') {
            current.push_back(text[i]);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

/**
 * @brief Strict unsigned parse. Rejects empty strings and trailing garbage,
 * which `atoi` would silently accept as 0.
 *
 */
static bool parseUint(const std::string& s, uint32_t& out)
{
    if (s.empty()) return false;
    uint32_t value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
        if (value > 1000000) return false;
    }
    out = value;
    return true;
}

/**
 * @brief Pull `# key: value` metadata out of a comment line.
 *
 */
static bool parseMetaLine(const std::string& line, std::string& key, std::string& value)
{
    std::string body = line.substr(1);
    size_t colon     = body.find(':');
    if (colon == std::string::npos) return false;
    key   = toLower(body.substr(0, colon));
    value = body.substr(colon + 1);
    trim(key);
    trim(value);
    return !key.empty() && !value.empty();
}

const char* model::intervalTypeName(IntervalType type)
{
    switch (type) {
        case IntervalType::Prep:
            return "prep";
        case IntervalType::Work:
            return "work";
        case IntervalType::Rest:
            return "rest";
        case IntervalType::Power:
            return "power";
        case IntervalType::Cooldown:
            return "cooldown";
    }
    return "work";
}

void Routine::recomputeTotal()
{
    totalSeconds = 0;
    for (const auto& interval : intervals) {
        totalSeconds += interval.seconds;
    }
}

/* -------------------------------------------------------------------------- */
/*                                Routine parse                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Apply a `repeat` directive: duplicate the trailing contiguous run of
 * intervals belonging to `section` so it plays `reps` times in total.
 *
 */
static bool applyRepeat(std::vector<Interval>& intervals, const std::string& section, uint32_t reps,
                        std::vector<std::string>& warnings, int lineNo)
{
    if (reps < 2) return true;

    // Walk back over the trailing run of this section
    size_t runEnd   = intervals.size();
    size_t runStart = runEnd;
    while (runStart > 0 && intervals[runStart - 1].section == section) {
        runStart--;
    }

    if (runStart == runEnd) {
        warnings.push_back("line " + std::to_string(lineNo) + ": repeat with no preceding rows in section '" + section +
                           "', ignored");
        return true;
    }

    const size_t runLength = runEnd - runStart;
    if (intervals.size() + runLength * (reps - 1) > kMaxIntervals) {
        warnings.push_back("line " + std::to_string(lineNo) + ": repeat would exceed the interval limit");
        return false;
    }

    for (uint32_t r = 1; r < reps; r++) {
        // Index rather than iterators: the vector reallocates as it grows
        for (size_t i = 0; i < runLength; i++) {
            intervals.push_back(intervals[runStart + i]);
        }
    }
    return true;
}

ParseResult model::parseRoutine(const std::string& csv, const std::string& fileName, Routine& out)
{
    ParseResult result;
    Routine routine;
    routine.file  = fileName;
    routine.title = fileName;

    const auto lines = splitLines(csv);
    if (lines.empty()) {
        result.error = "file is empty";
        return result;
    }

    bool headerSeen = false;
    int lineNo      = 0;

    for (const auto& rawLine : lines) {
        lineNo++;
        std::string line = rawLine;
        trim(line);
        if (line.empty()) continue;

        if (line[0] == '#') {
            std::string key, value;
            if (parseMetaLine(line, key, value)) {
                if (key == "title") {
                    routine.title = value;
                } else if (key == "author") {
                    routine.author = value;
                }
            }
            continue;
        }

        auto fields = splitFields(line);

        // First non-comment row must be the header
        if (!headerSeen) {
            headerSeen = true;
            if (fields.size() >= 4 && toLower(fields[0]) == "section" && toLower(fields[1]) == "label") {
                continue;
            }
            result.error = "missing header row 'section,label,type,seconds'";
            return result;
        }

        if (fields.size() < 4 || fields.size() > 5) {
            result.warnings.push_back("line " + std::to_string(lineNo) + ": expected 4 or 5 fields, got " +
                                      std::to_string(fields.size()) + " (a comma in a label?)");
            continue;
        }

        const std::string section  = fields[0];
        const std::string label    = fields[1];
        const std::string typeName = toLower(fields[2]);

        uint32_t reps = 1;
        if (fields.size() == 5 && !fields[4].empty()) {
            if (!parseUint(fields[4], reps) || reps < 1 || reps > kMaxReps) {
                result.warnings.push_back("line " + std::to_string(lineNo) + ": bad reps '" + fields[4] + "', using 1");
                reps = 1;
            }
        }

        if (typeName == "repeat") {
            if (!applyRepeat(routine.intervals, section, reps, result.warnings, lineNo)) {
                result.error = "routine exceeds " + std::to_string(kMaxIntervals) + " intervals";
                return result;
            }
            continue;
        }

        uint32_t seconds = 0;
        if (!parseUint(fields[3], seconds) || seconds < kMinSeconds || seconds > kMaxSeconds) {
            result.warnings.push_back("line " + std::to_string(lineNo) + ": bad seconds '" + fields[3] +
                                      "', row skipped");
            continue;
        }

        Interval interval;
        interval.section = section;
        interval.label   = label.empty() ? std::string(">") : label;
        interval.seconds = seconds;

        if (typeName == "work") {
            interval.type = IntervalType::Work;
        } else if (typeName == "rest") {
            interval.type = IntervalType::Rest;
        } else if (typeName == "power") {
            interval.type = IntervalType::Power;
        } else if (typeName == "prep") {
            interval.type = IntervalType::Prep;
        } else if (typeName == "cooldown") {
            interval.type = IntervalType::Cooldown;
        } else {
            interval.type = IntervalType::Work;
            result.warnings.push_back("line " + std::to_string(lineNo) + ": unknown type '" + fields[2] +
                                      "', treated as work");
        }

        if (interval.label.size() > kMaxLabelLength) {
            interval.label.resize(kMaxLabelLength);
        }

        // A row may carry its own reps, meaning "do this move N times back to back"
        for (uint32_t r = 0; r < reps; r++) {
            if (routine.intervals.size() >= kMaxIntervals) {
                result.error = "routine exceeds " + std::to_string(kMaxIntervals) + " intervals";
                return result;
            }
            routine.intervals.push_back(interval);
        }
    }

    if (!headerSeen) {
        result.error = "missing header row 'section,label,type,seconds'";
        return result;
    }
    if (routine.intervals.empty()) {
        result.error = "no valid intervals";
        return result;
    }

    routine.recomputeTotal();
    out       = std::move(routine);
    result.ok = true;
    return result;
}

/* -------------------------------------------------------------------------- */
/*                                 Index parse                                */
/* -------------------------------------------------------------------------- */

ParseResult model::parseIndex(const std::string& csv, std::vector<RoutineSummary>& out)
{
    ParseResult result;
    std::vector<RoutineSummary> summaries;

    bool headerSeen = false;
    int lineNo      = 0;

    for (const auto& rawLine : splitLines(csv)) {
        lineNo++;
        std::string line = rawLine;
        trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto fields = splitFields(line);

        if (!headerSeen) {
            headerSeen = true;
            if (!fields.empty() && toLower(fields[0]) == "file") {
                continue;
            }
            result.error = "missing header row 'file,title,minutes'";
            return result;
        }

        if (fields.size() < 2 || fields[0].empty()) {
            result.warnings.push_back("line " + std::to_string(lineNo) + ": malformed row, skipped");
            continue;
        }

        // Guard against path traversal — these names are used to build file paths
        if (fields[0].find('/') != std::string::npos || fields[0].find('\\') != std::string::npos ||
            fields[0].find("..") != std::string::npos) {
            result.warnings.push_back("line " + std::to_string(lineNo) + ": illegal file name '" + fields[0] +
                                      "', skipped");
            continue;
        }

        if (summaries.size() >= kMaxRoutines) {
            result.warnings.push_back("index truncated at " + std::to_string(kMaxRoutines) + " routines");
            break;
        }

        RoutineSummary summary;
        summary.file  = fields[0];
        summary.title = fields[1].empty() ? fields[0] : fields[1];
        if (fields.size() >= 3) {
            uint32_t minutes = 0;
            if (parseUint(fields[2], minutes)) {
                summary.totalSeconds = minutes * 60;
            }
        }
        summaries.push_back(std::move(summary));
    }

    if (!headerSeen) {
        result.error = "missing header row 'file,title,minutes'";
        return result;
    }
    if (summaries.empty()) {
        result.error = "index lists no routines";
        return result;
    }

    out       = std::move(summaries);
    result.ok = true;
    return result;
}

std::string model::buildIndexCsv(const std::vector<RoutineSummary>& summaries)
{
    std::string csv = "# Aqua Timer local cache index\nfile,title,minutes\n";
    for (const auto& summary : summaries) {
        csv += summary.file;
        csv += ',';
        csv += summary.title;
        csv += ',';
        csv += std::to_string((summary.totalSeconds + 59) / 60);
        csv += '\n';
    }
    return csv;
}
