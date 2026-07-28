/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "runner.h"
#include <hal/hal.h>
#include <mooncake_log.h>

using namespace model;

static const std::string_view _tag = "aqua-run";

namespace {

/* Returned when there is no current interval, so callers never dereference past the end. */
const Interval _empty_interval{};

}  // namespace

void RoutineRunner::load(const Routine& routine)
{
    _routine = routine;
    reset();
}

void RoutineRunner::reset()
{
    _state             = State::Idle;
    _index             = 0;
    _interval_start_ms = 0;
    _paused_elapsed_ms = 0;
    _last_cue_sec      = UINT32_MAX;
}

void RoutineRunner::start()
{
    if (_routine.intervals.empty()) {
        mclog::tagWarn(_tag, "refusing to start an empty routine");
        return;
    }

    _index             = 0;
    _last_cue_sec      = UINT32_MAX;
    _paused_elapsed_ms = 0;
    _interval_start_ms = GetHAL().millis();
    _state             = State::LeadIn;
}

void RoutineRunner::pause()
{
    if (_state != State::Running && _state != State::LeadIn) return;
    _paused_elapsed_ms = elapsedInIntervalMs();
    _state             = State::Paused;
}

void RoutineRunner::resume()
{
    if (_state != State::Paused) return;
    // Re-anchor so the time spent paused does not count against the interval
    _interval_start_ms = GetHAL().millis() - _paused_elapsed_ms;
    _paused_elapsed_ms = 0;
    _state             = State::Running;
}

void RoutineRunner::togglePause()
{
    if (_state == State::Paused) {
        resume();
    } else {
        pause();
    }
}

void RoutineRunner::next()
{
    if (!isActive()) return;

    if (_state == State::LeadIn) {
        // Skipping the lead-in just drops straight into the first interval
        enterInterval(0, true);
        return;
    }

    if (_index + 1 >= _routine.intervals.size()) {
        finish();
        return;
    }
    enterInterval(_index + 1, true);
}

void RoutineRunner::previous()
{
    if (!isActive() || _state == State::LeadIn) return;

    // Part-way through an interval, "back" restarts it rather than jumping away.
    // Matches how a physical lap button behaves and avoids over-shooting on a
    // mis-tap during a long move.
    if (elapsedInIntervalMs() > 2000 || _index == 0) {
        enterInterval(_index, true);
        return;
    }
    enterInterval(_index - 1, true);
}

/**
 * @brief Move to an interval and re-anchor the timeline.
 *
 * hardReset = true means the user redefined the timeline (skip, back, start), so
 * there is no accumulated error to carry. hardReset = false is a natural
 * roll-over, where the anchor is advanced by exactly the interval duration.
 *
 */
void RoutineRunner::enterInterval(size_t index, bool hardReset)
{
    const size_t previous_index = _index;
    _index                      = index;
    _paused_elapsed_ms          = 0;
    _last_cue_sec               = UINT32_MAX;

    if (hardReset) {
        _interval_start_ms = GetHAL().millis();
    } else {
        _interval_start_ms += _routine.intervals[previous_index].seconds * 1000;
    }

    _state = State::Running;
    _cues.fire(CueEvent::IntervalStart, _routine.intervals[_index].type);
}

void RoutineRunner::finish()
{
    _state        = State::Finished;
    _last_cue_sec = UINT32_MAX;
    _cues.fire(CueEvent::Finished, IntervalType::Cooldown);
    mclog::tagInfo(_tag, "routine finished: {}", _routine.title);
}

uint32_t RoutineRunner::currentDurationMs() const
{
    if (_state == State::LeadIn) {
        return kLeadInSeconds * 1000;
    }
    if (_index >= _routine.intervals.size()) {
        return 0;
    }
    return _routine.intervals[_index].seconds * 1000;
}

uint32_t RoutineRunner::elapsedInIntervalMs() const
{
    if (_state == State::Paused) {
        return _paused_elapsed_ms;
    }
    const uint32_t now = GetHAL().millis();
    // Unsigned wrap-around is correct here: millis() is a monotonic uint32 counter
    return now - _interval_start_ms;
}

void RoutineRunner::update()
{
    if (_state != State::Running && _state != State::LeadIn) return;

    const uint32_t duration_ms = currentDurationMs();
    const uint32_t elapsed_ms  = elapsedInIntervalMs();

    if (elapsed_ms >= duration_ms) {
        if (_state == State::LeadIn) {
            // Lead-in flows into interval 0 without drift correction: the routine
            // timeline starts here, so the anchor advancing by the lead-in length
            // is exactly right.
            _index = 0;
            _interval_start_ms += duration_ms;
            _last_cue_sec = UINT32_MAX;
            _state        = State::Running;
            _cues.fire(CueEvent::IntervalStart, _routine.intervals[0].type);
            return;
        }

        if (_index + 1 >= _routine.intervals.size()) {
            finish();
            return;
        }
        enterInterval(_index + 1, false);
        return;
    }

    // Countdown cues. Suppressed on very short intervals, where 3-2-1 is just noise.
    const uint32_t remaining_ms  = duration_ms - elapsed_ms;
    const uint32_t remaining_sec = (remaining_ms + 999) / 1000;

    if (remaining_sec == 0 || remaining_sec > kCountdownFromSec) return;
    if (_state == State::Running && duration_ms < kMinCountdownIntervalSec * 1000) return;
    if (remaining_sec == _last_cue_sec) return;

    _last_cue_sec = remaining_sec;
    _cues.fire(_state == State::LeadIn ? CueEvent::LeadInTick : CueEvent::CountdownTick, IntervalType::Work);
}

const Interval& RoutineRunner::getCurrentInterval() const
{
    if (_index >= _routine.intervals.size()) {
        return _empty_interval;
    }
    return _routine.intervals[_index];
}

std::string RoutineRunner::getNextLabel() const
{
    if (_state == State::LeadIn) {
        return _routine.intervals.empty() ? std::string() : _routine.intervals[0].label;
    }
    if (_index + 1 >= _routine.intervals.size()) {
        return {};
    }
    return _routine.intervals[_index + 1].label;
}

uint32_t RoutineRunner::getRemainingSeconds() const
{
    if (_state == State::Idle || _state == State::Finished) return 0;

    const uint32_t duration_ms = currentDurationMs();
    const uint32_t elapsed_ms  = elapsedInIntervalMs();
    if (elapsed_ms >= duration_ms) return 0;
    return (duration_ms - elapsed_ms + 999) / 1000;
}

uint32_t RoutineRunner::getRemainingTotalSeconds() const
{
    if (_state == State::Idle) return _routine.totalSeconds;
    if (_state == State::Finished) return 0;

    uint32_t remaining = getRemainingSeconds();
    if (_state == State::LeadIn) {
        return _routine.totalSeconds;
    }
    for (size_t i = _index + 1; i < _routine.intervals.size(); i++) {
        remaining += _routine.intervals[i].seconds;
    }
    return remaining;
}

uint32_t RoutineRunner::getProgressPermille() const
{
    if (_routine.totalSeconds == 0) return 0;
    if (_state == State::Finished) return 1000;

    const uint32_t remaining = getRemainingTotalSeconds();
    if (remaining >= _routine.totalSeconds) return 0;
    return static_cast<uint32_t>((_routine.totalSeconds - remaining) * 1000ULL / _routine.totalSeconds);
}
