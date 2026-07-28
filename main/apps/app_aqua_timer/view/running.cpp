/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <cstdio>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

/*
 * Interval type drives the whole background. Deep, saturated hues so the screen
 * still reads through wet goggles from the far side of the pool; the countdown
 * stays near-white against all of them.
 */
constexpr uint32_t _color_bg_work     = 0x0E4429;
constexpr uint32_t _color_bg_rest     = 0x0C2B4A;
constexpr uint32_t _color_bg_power    = 0x5A1A1A;
constexpr uint32_t _color_bg_easy     = 0x2B2B2B;
constexpr uint32_t _color_bg_leadin   = 0x3A2E05;
constexpr uint32_t _color_bg_finished = 0x123A2A;

constexpr uint32_t _color_countdown = 0xF2FAFF;
constexpr uint32_t _color_section   = 0xA8C4D4;
constexpr uint32_t _color_move      = 0xFFFFFF;
constexpr uint32_t _color_next      = 0x8A8A8A;
constexpr uint32_t _color_progress  = 0x7C8B93;
constexpr uint32_t _color_clock     = 0xCDEBF9;
constexpr uint32_t _color_muted     = 0xFFC46B;
constexpr uint32_t _color_arc       = 0x5CC8A0;
constexpr uint32_t _color_arc_bg    = 0x263038;

uint32_t bg_color_for(const model::RoutineRunner& runner)
{
    switch (runner.getState()) {
        case model::RoutineRunner::State::LeadIn:
            return _color_bg_leadin;
        case model::RoutineRunner::State::Finished:
            return _color_bg_finished;
        default:
            break;
    }

    switch (runner.getCurrentInterval().type) {
        case model::IntervalType::Work:
            return _color_bg_work;
        case model::IntervalType::Rest:
            return _color_bg_rest;
        case model::IntervalType::Power:
            return _color_bg_power;
        case model::IntervalType::Prep:
        case model::IntervalType::Cooldown:
            return _color_bg_easy;
    }
    return _color_bg_easy;
}

std::string format_mmss(uint32_t seconds)
{
    char buffer[16] = {};
    snprintf(buffer, sizeof(buffer), "%u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
    return std::string(buffer);
}

std::string to_upper(std::string text)
{
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') c -= 32;
    }
    return text;
}

}  // namespace

RunningPage::RunningPage(model::RoutineRunner* runner) : _runner(runner)
{
    build(lv_screen_active());
}

RunningPage::~RunningPage()
{
    // Children are owned by _panel; destroying it takes the arc with it
    _btn_end.reset();
    _btn_next.reset();
    _btn_pause.reset();
    _btn_prev.reset();
    _tap_zone.reset();
    _label_progress.reset();
    _label_next.reset();
    _label_move.reset();
    _label_countdown.reset();
    _label_section.reset();
    _label_muted.reset();
    _clock.reset();
    _arc_progress = nullptr;
    _panel.reset();
}

void RunningPage::build(lv_obj_t* parent)
{
    _panel = std::make_unique<Container>(parent);
    _panel->setSize(466, 466);
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setBgColor(lv_color_hex(_color_bg_easy));
    _panel->setBorderWidth(0);
    _panel->setRadius(233);
    _panel->setPadding(0, 0, 0, 0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(_panel->get(), LV_OBJ_FLAG_SCROLLABLE);

    // Perimeter progress ring. Raw lv_arc: the C++ wrapper has no arc widget.
    _arc_progress = lv_arc_create(_panel->get());
    lv_obj_set_size(_arc_progress, 460, 460);
    lv_obj_center(_arc_progress);
    lv_arc_set_rotation(_arc_progress, 270);  // start at 12 o'clock
    lv_arc_set_bg_angles(_arc_progress, 0, 360);
    lv_arc_set_range(_arc_progress, 0, 1000);
    lv_arc_set_value(_arc_progress, 0);
    lv_obj_remove_style(_arc_progress, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(_arc_progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(_arc_progress, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_arc_progress, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_arc_progress, lv_color_hex(_color_arc_bg), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_arc_progress, lv_color_hex(_color_arc), LV_PART_INDICATOR);

    _clock        = std::make_unique<ArcTopClock>(_panel->get());
    _clock->color = _color_clock;
    _clock->init();
    _clock->align(LV_ALIGN_TOP_MID, 0, 4);

    _label_muted = std::make_unique<Label>(*_panel);
    _label_muted->setTextFont(&lv_font_montserrat_16);
    _label_muted->setTextColor(lv_color_hex(_color_muted));
    _label_muted->setText("MUTED");
    _label_muted->align(LV_ALIGN_TOP_MID, 0, 62);
    _label_muted->setHidden(true);

    _label_section = std::make_unique<Label>(*_panel);
    _label_section->setTextFont(&MontserratSemiBold26);
    _label_section->setTextColor(lv_color_hex(_color_section));
    _label_section->setWidth(360);
    _label_section->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_section->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    _label_section->align(LV_ALIGN_CENTER, 0, -112);

    _label_countdown = std::make_unique<Label>(*_panel);
    _label_countdown->setTextFont(&CommissionerMedium108);
    _label_countdown->setTextColor(lv_color_hex(_color_countdown));
    _label_countdown->align(LV_ALIGN_CENTER, 0, -34);

    _label_move = std::make_unique<Label>(*_panel);
    _label_move->setTextFont(&lv_font_maple_mono_medium_28);
    _label_move->setTextColor(lv_color_hex(_color_move));
    _label_move->setWidth(380);
    _label_move->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_move->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    _label_move->align(LV_ALIGN_CENTER, 0, 34);

    _label_next = std::make_unique<Label>(*_panel);
    _label_next->setTextFont(&lv_font_montserrat_22);
    _label_next->setTextColor(lv_color_hex(_color_next));
    _label_next->setWidth(360);
    _label_next->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_next->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    _label_next->align(LV_ALIGN_CENTER, 0, 72);

    _label_progress = std::make_unique<Label>(*_panel);
    _label_progress->setTextFont(&lv_font_montserrat_18);
    _label_progress->setTextColor(lv_color_hex(_color_progress));
    _label_progress->align(LV_ALIGN_CENTER, 0, 106);

    /*
     * Tap-anywhere pause. Wet hands and a round screen make small targets a bad
     * bet, so the whole middle band toggles pause. It sits above the labels but
     * clear of the prev/next buttons, which stay deliberately small so they are
     * hard to hit by accident.
     */
    _tap_zone = std::make_unique<Container>(*_panel);
    _tap_zone->setSize(400, 240);
    _tap_zone->align(LV_ALIGN_CENTER, 0, -20);
    _tap_zone->setBgOpa(LV_OPA_TRANSP);
    _tap_zone->setBorderWidth(0);
    _tap_zone->setRadius(0);
    lv_obj_remove_flag(_tap_zone->get(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_tap_zone->get(), LV_OBJ_FLAG_CLICKABLE);
    _tap_zone->onClick().connect([this]() {
        if (_runner->isActive()) _runner->togglePause();
    });

    const auto style_small_button = [](Button* button, uint32_t color) {
        button->setSize(78, 62);
        button->setRadius(31);
        button->setBorderWidth(0);
        button->setShadowWidth(0);
        button->setBgColor(lv_color_hex(color));
        button->label().setTextFont(&lv_font_montserrat_28);
        button->label().setTextColor(lv_color_hex(0xE8F2F7));
    };

    _btn_prev = std::make_unique<Button>(*_panel);
    style_small_button(_btn_prev.get(), 0x39454C);
    _btn_prev->label().setText("<");
    _btn_prev->align(LV_ALIGN_CENTER, -128, 158);
    _btn_prev->onClick().connect([this]() { _runner->previous(); });

    _btn_pause = std::make_unique<Button>(*_panel);
    _btn_pause->setSize(148, 68);
    _btn_pause->setRadius(34);
    _btn_pause->setBorderWidth(0);
    _btn_pause->setShadowWidth(0);
    _btn_pause->setBgColor(lv_color_hex(0x4E5C64));
    _btn_pause->label().setTextFont(&lv_font_montserrat_22);
    _btn_pause->label().setTextColor(lv_color_hex(0xFFFFFF));
    _btn_pause->label().setText("PAUSE");
    _btn_pause->align(LV_ALIGN_CENTER, 0, 156);
    _btn_pause->onClick().connect([this]() {
        if (_runner->getState() == model::RoutineRunner::State::Finished) {
            _exit_requested = true;
        } else {
            _runner->togglePause();
        }
    });

    _btn_next = std::make_unique<Button>(*_panel);
    style_small_button(_btn_next.get(), 0x39454C);
    _btn_next->label().setText(">");
    _btn_next->align(LV_ALIGN_CENTER, 128, 158);
    _btn_next->onClick().connect([this]() { _runner->next(); });

    /*
     * End Class only exists while paused. Pausing first makes ending a
     * deliberate two-step, so a stray palm cannot throw away 30 minutes of
     * class position — and it keeps the running screen uncluttered.
     */
    _btn_end = std::make_unique<Button>(*_panel);
    _btn_end->setSize(180, 46);
    _btn_end->setRadius(23);
    _btn_end->setBorderWidth(0);
    _btn_end->setShadowWidth(0);
    _btn_end->setBgColor(lv_color_hex(0x6B2230));
    _btn_end->label().setTextFont(&lv_font_montserrat_18);
    _btn_end->label().setTextColor(lv_color_hex(0xFFD8DE));
    _btn_end->label().setText("END CLASS");
    _btn_end->align(LV_ALIGN_CENTER, 0, 206);
    _btn_end->setHidden(true);
    _btn_end->onClick().connect([this]() { _exit_requested = true; });

    applyStateStyling();
}

void RunningPage::setTextIfChanged(Label* label, std::string& cache, const std::string& text)
{
    if (cache == text) return;
    cache = text;
    label->setText(text);
}

void RunningPage::applyStateStyling()
{
    const uint32_t bg = bg_color_for(*_runner);
    if (bg != _shown_bg_color) {
        _shown_bg_color = bg;
        _panel->setBgColor(lv_color_hex(bg));
    }

    const auto state = _runner->getState();
    if (state == _shown_state) return;
    _shown_state = state;

    const bool paused   = state == model::RoutineRunner::State::Paused;
    const bool finished = state == model::RoutineRunner::State::Finished;

    _btn_end->setHidden(!paused);
    _btn_prev->setHidden(finished);
    _btn_next->setHidden(finished);

    if (finished) {
        _btn_pause->label().setText("DONE");
        _btn_pause->setBgColor(lv_color_hex(0x2E6B4F));
    } else if (paused) {
        _btn_pause->label().setText("RESUME");
        _btn_pause->setBgColor(lv_color_hex(0x2E6B4F));
    } else {
        _btn_pause->label().setText("PAUSE");
        _btn_pause->setBgColor(lv_color_hex(0x4E5C64));
    }
}

void RunningPage::update()
{
    if (_exit_requested) {
        _exit_requested = false;
        if (onExit) onExit();
        return;
    }

    _clock->update();
    _runner->update();
    applyStateStyling();

    _label_muted->setHidden(!_runner->cues().isMuted());

    const auto state = _runner->getState();

    if (state == model::RoutineRunner::State::LeadIn) {
        setTextIfChanged(_label_section.get(), _shown_section, "GET READY");
        setTextIfChanged(_label_countdown.get(), _shown_countdown, std::to_string(_runner->getRemainingSeconds()));
        setTextIfChanged(_label_move.get(), _shown_move, to_upper(_runner->getRoutine().title));
        setTextIfChanged(_label_next.get(), _shown_next, "FIRST  " + _runner->getNextLabel());
        setTextIfChanged(_label_progress.get(), _shown_progress,
                         std::to_string(_runner->getIntervalCount()) + " moves  |  " +
                             format_mmss(_runner->getRoutine().totalSeconds));
        return;
    }

    if (state == model::RoutineRunner::State::Finished) {
        setTextIfChanged(_label_section.get(), _shown_section, "CLASS COMPLETE");
        setTextIfChanged(_label_countdown.get(), _shown_countdown, "0:00");
        setTextIfChanged(_label_move.get(), _shown_move, to_upper(_runner->getRoutine().title));
        setTextIfChanged(_label_next.get(), _shown_next, "");
        setTextIfChanged(_label_progress.get(), _shown_progress,
                         std::to_string(_runner->getIntervalCount()) + " moves done");
        lv_arc_set_value(_arc_progress, 1000);
        return;
    }

    const auto& interval = _runner->getCurrentInterval();

    setTextIfChanged(_label_section.get(), _shown_section, to_upper(interval.section));
    setTextIfChanged(_label_countdown.get(), _shown_countdown, format_mmss(_runner->getRemainingSeconds()));
    setTextIfChanged(_label_move.get(), _shown_move, to_upper(interval.label));

    const std::string next = _runner->getNextLabel();
    setTextIfChanged(_label_next.get(), _shown_next, next.empty() ? "LAST MOVE" : "NEXT  " + next);

    setTextIfChanged(_label_progress.get(), _shown_progress,
                     std::to_string(_runner->getIndex() + 1) + "/" + std::to_string(_runner->getIntervalCount()) +
                         "  |  " + format_mmss(_runner->getRemainingTotalSeconds()) + " left");

    lv_arc_set_value(_arc_progress, static_cast<int32_t>(_runner->getProgressPermille()));
}
