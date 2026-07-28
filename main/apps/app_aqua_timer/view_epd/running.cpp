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
 * 16-grey e-ink palette. Polarity is the cue: Work and Power run inverted
 * (black page, white ink) so the state reads across a pool deck; Rest and the
 * easy intervals run normal (white page, black ink). Mid-greys carry the
 * secondary text either way.
 */
constexpr uint32_t _ink_black    = 0x000000;
constexpr uint32_t _ink_white    = 0xFFFFFF;
constexpr uint32_t _ink_grey     = 0x555555;
constexpr uint32_t _ink_grey_inv = 0xAAAAAA;

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

bool inverted_for(const model::RoutineRunner& runner)
{
    switch (runner.getState()) {
        case model::RoutineRunner::State::LeadIn:
            return false;
        case model::RoutineRunner::State::Finished:
            return true;
        default:
            break;
    }
    switch (runner.getCurrentInterval().type) {
        case model::IntervalType::Work:
        case model::IntervalType::Power:
            return true;
        case model::IntervalType::Rest:
        case model::IntervalType::Prep:
        case model::IntervalType::Cooldown:
            return false;
    }
    return false;
}

}  // namespace

RunningPage::RunningPage(model::RoutineRunner* runner) : _runner(runner)
{
    build(lv_screen_active());
}

RunningPage::~RunningPage()
{
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
    _bar_progress = nullptr;
    _panel.reset();
}

void RunningPage::build(lv_obj_t* parent)
{
    _panel = std::make_unique<Container>(parent);
    _panel->setSize(540, 960);
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setBgColor(lv_color_hex(_ink_white));
    _panel->setBorderWidth(0);
    _panel->setRadius(0);
    _panel->setPadding(0, 0, 0, 0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(_panel->get(), LV_OBJ_FLAG_SCROLLABLE);

    _label_section = std::make_unique<Label>(*_panel);
    _label_section->setTextFont(&lv_font_montserrat_48);
    _label_section->setWidth(500);
    _label_section->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_section->setLongMode(LV_LABEL_LONG_MODE_DOTS);
    _label_section->align(LV_ALIGN_TOP_MID, 0, 36);

    _label_countdown = std::make_unique<Label>(*_panel);
    _label_countdown->setTextFont(&CommissionerMedium192);
    _label_countdown->align(LV_ALIGN_TOP_MID, 0, 140);
    // Chip styling (used for the last-3-seconds tick): rounded pad behind the
    // digits in opposite polarity, toggled via bg opacity.
    lv_obj_set_style_pad_hor(_label_countdown->get(), 28, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(_label_countdown->get(), 8, LV_PART_MAIN);
    lv_obj_set_style_radius(_label_countdown->get(), 24, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_label_countdown->get(), LV_OPA_TRANSP, LV_PART_MAIN);

    _label_move = std::make_unique<Label>(*_panel);
    _label_move->setTextFont(&lv_font_montserrat_48);
    _label_move->setWidth(500);
    _label_move->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_move->setLongMode(LV_LABEL_LONG_MODE_DOTS);
    _label_move->align(LV_ALIGN_TOP_MID, 0, 430);

    _label_next = std::make_unique<Label>(*_panel);
    _label_next->setTextFont(&lv_font_montserrat_36);
    _label_next->setWidth(500);
    _label_next->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_next->setLongMode(LV_LABEL_LONG_MODE_DOTS);
    _label_next->align(LV_ALIGN_TOP_MID, 0, 560);

    // Linear progress bar instead of the AMOLED build's perimeter arc; updated
    // at most once per second to keep e-ink churn down.
    _bar_progress = lv_bar_create(_panel->get());
    lv_obj_set_size(_bar_progress, 500, 24);
    lv_obj_align(_bar_progress, LV_ALIGN_TOP_MID, 0, 640);
    lv_bar_set_range(_bar_progress, 0, 1000);
    lv_bar_set_value(_bar_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(_bar_progress, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(_bar_progress, 4, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(_bar_progress, 2, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(_bar_progress, 0, LV_PART_MAIN);

    _label_progress = std::make_unique<Label>(*_panel);
    _label_progress->setTextFont(&lv_font_montserrat_28);
    _label_progress->align(LV_ALIGN_TOP_MID, 0, 690);

    /*
     * Tap-anywhere pause: everything above the button row toggles pause. Wet
     * hands and pool-deck haste make small targets a bad bet.
     */
    _tap_zone = std::make_unique<Container>(*_panel);
    _tap_zone->setSize(540, 680);
    _tap_zone->align(LV_ALIGN_TOP_MID, 0, 0);
    _tap_zone->setBgOpa(LV_OPA_TRANSP);
    _tap_zone->setBorderWidth(0);
    _tap_zone->setRadius(0);
    lv_obj_remove_flag(_tap_zone->get(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_tap_zone->get(), LV_OBJ_FLAG_CLICKABLE);
    _tap_zone->onClick().connect([this]() {
        if (_runner->isActive()) _runner->togglePause();
    });

    const auto style_button = [](Button* button) {
        button->setRadius(8);
        button->setBorderWidth(3);
        button->setShadowWidth(0);
        button->label().setTextFont(&lv_font_montserrat_36);
    };

    _btn_prev = std::make_unique<Button>(*_panel);
    style_button(_btn_prev.get());
    _btn_prev->setSize(140, 110);
    _btn_prev->label().setText("<");
    _btn_prev->align(LV_ALIGN_BOTTOM_MID, -185, -40);
    _btn_prev->onClick().connect([this]() { _runner->previous(); });

    _btn_pause = std::make_unique<Button>(*_panel);
    style_button(_btn_pause.get());
    _btn_pause->setSize(210, 110);
    _btn_pause->label().setText("PAUSE");
    _btn_pause->label().setTextFont(&lv_font_montserrat_28);
    _btn_pause->align(LV_ALIGN_BOTTOM_MID, 0, -40);
    _btn_pause->onClick().connect([this]() {
        if (_runner->getState() == model::RoutineRunner::State::Finished) {
            _exit_requested = true;
        } else {
            _runner->togglePause();
        }
    });

    _btn_next = std::make_unique<Button>(*_panel);
    style_button(_btn_next.get());
    _btn_next->setSize(140, 110);
    _btn_next->label().setText(">");
    _btn_next->align(LV_ALIGN_BOTTOM_MID, 185, -40);
    _btn_next->onClick().connect([this]() { _runner->next(); });

    /*
     * End Class only exists while paused: a stray palm cannot throw away 30
     * minutes of class position.
     */
    _btn_end = std::make_unique<Button>(*_panel);
    style_button(_btn_end.get());
    _btn_end->setSize(320, 80);
    _btn_end->label().setText("END CLASS");
    _btn_end->label().setTextFont(&lv_font_montserrat_28);
    _btn_end->align(LV_ALIGN_BOTTOM_MID, 0, -170);
    _btn_end->setHidden(true);
    _btn_end->onClick().connect([this]() { _exit_requested = true; });

    applyPolarity(false, false);
    applyStateStyling();
    // Clean slate: wipe menu/picker ghosting once the first frame is rendered
    GetHAL().requestEpdFullRefresh();
}

void RunningPage::setTextIfChanged(Label* label, std::string& cache, const std::string& text)
{
    if (cache == text) return;
    cache = text;
    label->setText(text);
}

void RunningPage::applyPolarity(bool inverted, bool powerFrame)
{
    const lv_color_t bg      = lv_color_hex(inverted ? _ink_black : _ink_white);
    const lv_color_t ink     = lv_color_hex(inverted ? _ink_white : _ink_black);
    const lv_color_t ink_dim = lv_color_hex(inverted ? _ink_grey_inv : _ink_grey);

    _panel->setBgColor(bg);
    // Power gets a thick contrasting frame on top of the inverted page
    _panel->setBorderWidth(powerFrame ? 14 : 0);
    _panel->setBorderColor(ink);

    _label_section->setTextColor(ink_dim);
    _label_countdown->setTextColor(ink);
    lv_obj_set_style_bg_color(_label_countdown->get(), ink, LV_PART_MAIN);
    _label_move->setTextColor(ink);
    _label_next->setTextColor(ink_dim);
    _label_progress->setTextColor(ink_dim);

    lv_obj_set_style_border_color(_bar_progress, ink, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_bar_progress, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_bar_progress, ink, LV_PART_INDICATOR);

    for (Button* button : {_btn_prev.get(), _btn_pause.get(), _btn_next.get(), _btn_end.get()}) {
        button->setBgColor(bg);
        button->setBorderColor(ink);
        button->label().setTextColor(ink);
    }

    _shown_chip = false;  // chip colors are polarity-relative, force re-apply
    setCountdownChip(false);
}

void RunningPage::setCountdownChip(bool show)
{
    if (show == _shown_chip) return;
    _shown_chip = show;

    // The chip flips the digits to the opposite polarity of the page
    lv_obj_set_style_bg_opa(_label_countdown->get(), show ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    const lv_color_t digits = show ? lv_color_hex(_shown_inverted ? _ink_black : _ink_white)
                                   : lv_color_hex(_shown_inverted ? _ink_white : _ink_black);
    _label_countdown->setTextColor(digits);
}

void RunningPage::applyStateStyling()
{
    const bool inverted = inverted_for(*_runner);
    const auto state    = _runner->getState();

    if (inverted != _shown_inverted) {
        _shown_inverted = inverted;
        applyPolarity(inverted, state == model::RoutineRunner::State::Running &&
                                    _runner->getCurrentInterval().type == model::IntervalType::Power);
    }

    if (state != _shown_state) {
        const bool paused   = state == model::RoutineRunner::State::Paused;
        const bool finished = state == model::RoutineRunner::State::Finished;

        _btn_end->setHidden(!paused);
        _btn_prev->setHidden(finished);
        _btn_next->setHidden(finished);
        _btn_pause->label().setText(finished ? "DONE" : (paused ? "RESUME" : "PAUSE"));
    }

    _shown_state = state;
    _shown_index = _runner->getIndex();
}

void RunningPage::update()
{
    if (_exit_requested) {
        _exit_requested = false;
        if (onExit) onExit();
        return;
    }

    _runner->update();

    const auto state   = _runner->getState();
    const size_t index = _runner->getIndex();

    /*
     * The quality refresh both clears ghosting from the previous interval and
     * produces the full-screen flash that IS the interval-change cue. It fires
     * whenever the timeline moves: lead-in entry, every interval roll-over,
     * skip/back, and finish — but not on pause/resume, where nothing advanced.
     */
    const bool pause_toggle =
        (state == model::RoutineRunner::State::Paused && _shown_state == model::RoutineRunner::State::Running) ||
        (state == model::RoutineRunner::State::Running && _shown_state == model::RoutineRunner::State::Paused);
    const bool timeline_moved = (state != _shown_state) || (index != _shown_index);

    applyStateStyling();

    if (timeline_moved && !pause_toggle) {
        GetHAL().requestEpdFullRefresh();
    }

    if (state == model::RoutineRunner::State::LeadIn) {
        setTextIfChanged(_label_section.get(), _shown_section, "GET READY");
        setTextIfChanged(_label_countdown.get(), _shown_countdown, std::to_string(_runner->getRemainingSeconds()));
        setTextIfChanged(_label_move.get(), _shown_move, to_upper(_runner->getRoutine().title));
        setTextIfChanged(_label_next.get(), _shown_next, "FIRST  " + _runner->getNextLabel());
        setTextIfChanged(_label_progress.get(), _shown_progress,
                         std::to_string(_runner->getIntervalCount()) + " moves  |  " +
                             format_mmss(_runner->getRoutine().totalSeconds));
        setCountdownChip(true);
        return;
    }

    if (state == model::RoutineRunner::State::Finished) {
        setTextIfChanged(_label_section.get(), _shown_section, "CLASS COMPLETE");
        setTextIfChanged(_label_countdown.get(), _shown_countdown, "0:00");
        setTextIfChanged(_label_move.get(), _shown_move, to_upper(_runner->getRoutine().title));
        setTextIfChanged(_label_next.get(), _shown_next, "");
        setTextIfChanged(_label_progress.get(), _shown_progress,
                         std::to_string(_runner->getIntervalCount()) + " moves done");
        if (_shown_bar_value != 1000) {
            _shown_bar_value = 1000;
            lv_bar_set_value(_bar_progress, 1000, LV_ANIM_OFF);
        }
        setCountdownChip(false);
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

    // Last-3-seconds chip: the countdown digits flip polarity as the tick cue
    setCountdownChip(state == model::RoutineRunner::State::Running && _runner->getRemainingSeconds() <= 3);

    // Quantize bar movement to once per second (the progress permille moves in
    // sub-second steps on long routines and would spam partial refreshes)
    const int32_t bar_value = (int32_t)_runner->getProgressPermille();
    const int32_t bar_delta =
        bar_value > _shown_bar_value ? bar_value - _shown_bar_value : _shown_bar_value - bar_value;
    if (bar_value != _shown_bar_value && (bar_value == 0 || bar_value == 1000 || bar_delta >= 5)) {
        _shown_bar_value = bar_value;
        lv_bar_set_value(_bar_progress, bar_value, LV_ANIM_OFF);
    }
}
