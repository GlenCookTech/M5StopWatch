/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace model {

/**
 * @brief What kind of interval this is. Drives cue pattern and background colour.
 *
 */
enum class IntervalType { Prep, Work, Rest, Power, Cooldown };

const char* intervalTypeName(IntervalType type);

/**
 * @brief A single timed step of a routine.
 *
 */
struct Interval {
    std::string section;
    std::string label;
    IntervalType type = IntervalType::Work;
    uint32_t seconds  = 0;
};

/**
 * @brief A fully expanded routine. `repeat` directives are resolved at parse time,
 * so the runtime only ever walks a flat vector.
 *
 */
struct Routine {
    std::string file;
    std::string title;
    std::string author;
    std::vector<Interval> intervals;
    uint32_t totalSeconds = 0;

    void recomputeTotal();
};

/**
 * @brief One line of the routine index, used to build the picker without
 * loading and parsing every cached routine.
 *
 */
struct RoutineSummary {
    std::string file;
    std::string title;
    uint32_t totalSeconds = 0;
    size_t intervalCount  = 0;
};

/* Guard rails. A malformed or hostile file must not be able to exhaust RAM. */
constexpr size_t kMaxIntervals       = 500;
constexpr size_t kMaxRoutines        = 32;
constexpr uint32_t kMinSeconds       = 1;
constexpr uint32_t kMaxSeconds       = 3600;
constexpr uint32_t kMaxReps          = 20;
constexpr size_t kMaxLabelLength     = 64;
constexpr uint32_t kCountdownFromSec = 3;
/* Below this length a 3-2-1 countdown is just noise, so it is suppressed. */
constexpr uint32_t kMinCountdownIntervalSec = 5;

}  // namespace model
