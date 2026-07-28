/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "routine.h"

namespace model {

/**
 * @brief What the runner is telling the instructor about.
 *
 */
enum class CueEvent {
    None,
    LeadInTick,     ///< 3-2-1 before the routine starts
    IntervalStart,  ///< a new interval just began
    CountdownTick,  ///< 3, 2, 1 remaining in the current interval
    Finished,       ///< routine complete
};

enum class CueMode {
    SoundAndBuzz = 0,
    BuzzOnly     = 1,
};

/**
 * @brief Turns runner events into vibration and audio.
 *
 * All calls are non-blocking: Hal::vibrate notifies a dedicated task, and
 * Hal::audioPlay defaults to async.
 *
 * Every pattern in cues.cpp is deliberately kept in one table — cue
 * perceptibility at a real pool cannot be verified from a desk, so expect to
 * retune it after the first class.
 *
 */
class CueEngine {
public:
    void setMode(CueMode mode)
    {
        _mode = mode;
    }
    CueMode getMode() const
    {
        return _mode;
    }

    /** @brief True when audio cues would be silently swallowed by a zero volume. */
    bool isMuted() const;

    void fire(CueEvent event, IntervalType type);

private:
    CueMode _mode = CueMode::SoundAndBuzz;

    void buzz(uint16_t durationMs, uint8_t strength);
    void tone(int midi, float durationSec, float volume);
    void melody(const std::vector<int>& midiList, float durationSec, float volume);
};

}  // namespace model
