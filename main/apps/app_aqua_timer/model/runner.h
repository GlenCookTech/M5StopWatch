/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "cues.h"
#include "routine.h"
#include <cstdint>
#include <string>

namespace model {

/**
 * @brief Walks a routine in real time and fires cues.
 *
 * Polled from the app loop via update(); there are no timers or callbacks, matching
 * the existing Stopwatch app.
 *
 */
class RoutineRunner {
public:
    enum class State { Idle, LeadIn, Running, Paused, Finished };

    /* Seconds of "3-2-1-GO" before interval 0, so the class can get into position. */
    static constexpr uint32_t kLeadInSeconds = 3;

    void load(const Routine& routine);

    void start();
    void pause();
    void resume();
    void togglePause();
    void next();
    void previous();
    void reset();

    /** @brief Advance time and fire any due cues. Call every app loop iteration. */
    void update();

    State getState() const
    {
        return _state;
    }
    bool isActive() const
    {
        return _state == State::LeadIn || _state == State::Running || _state == State::Paused;
    }

    const Routine& getRoutine() const
    {
        return _routine;
    }
    size_t getIndex() const
    {
        return _index;
    }
    size_t getIntervalCount() const
    {
        return _routine.intervals.size();
    }

    const Interval& getCurrentInterval() const;
    /** @brief Empty label when the current interval is the last one. */
    std::string getNextLabel() const;

    /** @brief Seconds left in the current interval (or in the lead-in), rounded up. */
    uint32_t getRemainingSeconds() const;
    /** @brief Seconds left in the whole routine, current interval included. */
    uint32_t getRemainingTotalSeconds() const;
    /** @brief 0..1000, for the perimeter progress arc. */
    uint32_t getProgressPermille() const;

    CueEngine& cues()
    {
        return _cues;
    }

private:
    Routine _routine;
    CueEngine _cues;
    State _state = State::Idle;

    size_t _index = 0;
    /* Timeline anchor for the current interval. Advanced by exactly the interval
       duration on a natural roll-over so scheduling error cannot accumulate. */
    uint32_t _interval_start_ms = 0;
    uint32_t _paused_elapsed_ms = 0;
    /* Guards against firing the same countdown second twice within one interval. */
    uint32_t _last_cue_sec = UINT32_MAX;

    uint32_t currentDurationMs() const;
    uint32_t elapsedInIntervalMs() const;
    void enterInterval(size_t index, bool hardReset);
    void finish();
};

}  // namespace model
