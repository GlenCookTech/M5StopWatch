/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "cues.h"
#include <apps/common/audio/audio.h>
#include <hal/hal.h>

using namespace model;

namespace {

/*
 * Cue table.
 *
 * Work and rest are made distinguishable on three axes at once — pitch
 * direction, note count, and buzz length — because at a noisy pool any single
 * axis can be masked. Retune these after teaching with it.
 */
constexpr uint16_t _buzz_work_ms      = 300;
constexpr uint16_t _buzz_rest_ms      = 120;
constexpr uint16_t _buzz_power_ms     = 450;
constexpr uint16_t _buzz_easy_ms      = 150;
constexpr uint16_t _buzz_countdown_ms = 60;
constexpr uint16_t _buzz_finish_ms    = 700;

constexpr uint8_t _strength_full   = 100;
constexpr uint8_t _strength_medium = 70;
constexpr uint8_t _strength_soft   = 60;

}  // namespace

bool CueEngine::isMuted() const
{
    if (_mode == CueMode::BuzzOnly) {
        return false;  // Buzz-only is a deliberate choice, not a fault
    }
    return GetHAL().getSpeakerVolume() <= 0;
}

void CueEngine::buzz(uint16_t durationMs, uint8_t strength)
{
    GetHAL().vibrate(durationMs, strength);
}

void CueEngine::tone(int midi, float durationSec, float volume)
{
    if (_mode == CueMode::BuzzOnly) return;
    audio::play_tone_from_midi(midi, durationSec, volume);
}

void CueEngine::melody(const std::vector<int>& midiList, float durationSec, float volume)
{
    if (_mode == CueMode::BuzzOnly) return;
    audio::play_melody(midiList, durationSec, volume);
}

void CueEngine::fire(CueEvent event, IntervalType type)
{
    switch (event) {
        case CueEvent::LeadInTick:
        case CueEvent::CountdownTick:
            buzz(_buzz_countdown_ms, _strength_soft);
            tone(72, 0.06f, 1.0f);
            break;

        case CueEvent::IntervalStart:
            switch (type) {
                case IntervalType::Work:
                    // Rising double — bright, unmistakably "go"
                    buzz(_buzz_work_ms, _strength_full);
                    melody({76, 88}, 0.09f, 1.0f);
                    break;
                case IntervalType::Rest:
                    // Single low note, short buzz
                    buzz(_buzz_rest_ms, _strength_medium);
                    tone(64, 0.20f, 1.0f);
                    break;
                case IntervalType::Power:
                    // Triple rising, longest buzz
                    buzz(_buzz_power_ms, _strength_full);
                    melody({76, 83, 88}, 0.08f, 1.0f);
                    break;
                case IntervalType::Prep:
                case IntervalType::Cooldown:
                    buzz(_buzz_easy_ms, _strength_soft);
                    tone(69, 0.15f, 0.8f);
                    break;
            }
            break;

        case CueEvent::Finished:
            buzz(_buzz_finish_ms, _strength_full);
            melody({72, 76, 79, 84}, 0.12f, 1.0f);
            break;

        case CueEvent::None:
            break;
    }
}
