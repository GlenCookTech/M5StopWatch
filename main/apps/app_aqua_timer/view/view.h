/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../model/config.h"
#include "../model/routine.h"
#include "../model/runner.h"

#include <apps/common/arc_top_clock/arc_top_clock.h>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace view {

/**
 * @brief The screen the instructor actually coaches from.
 *
 * Designed to be read across a pool deck: the whole background colour codes the
 * interval type, because at 15 m through wet goggles colour carries and text
 * does not.
 *
 */
class RunningPage {
public:
    RunningPage(model::RoutineRunner* runner);
    ~RunningPage();

    /** @brief Instructor ended the class, or it finished on its own. */
    std::function<void()> onExit;

    void update();

private:
    model::RoutineRunner* _runner = nullptr;

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<view::ArcTopClock> _clock;
    lv_obj_t* _arc_progress = nullptr;

    std::unique_ptr<uitk::lvgl_cpp::Label> _label_muted;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_section;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_countdown;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_move;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_next;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_progress;

    std::unique_ptr<uitk::lvgl_cpp::Container> _tap_zone;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_prev;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_pause;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_next;
    /* Only shown while paused: ending a class is a deliberate two-step. */
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_end;

    /* Last rendered strings. LVGL relayouts on every setText(), and this screen
       updates ~60x a second, so only changed text is pushed. */
    std::string _shown_section;
    std::string _shown_countdown;
    std::string _shown_move;
    std::string _shown_next;
    std::string _shown_progress;
    uint32_t _shown_bg_color                 = 0;
    model::RoutineRunner::State _shown_state = model::RoutineRunner::State::Idle;
    bool _exit_requested                     = false;

    void build(lv_obj_t* parent);
    void setTextIfChanged(uitk::lvgl_cpp::Label* label, std::string& cache, const std::string& text);
    void applyStateStyling();
};

/**
 * @brief Scrollable list of cached routines.
 *
 */
class RoutinePickerPage {
public:
    RoutinePickerPage(const std::vector<model::RoutineSummary>& routines);
    ~RoutinePickerPage();

    std::function<void(const std::string& file)> onRoutineSelected;
    std::function<void()> onBack;

    void update();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Label>> _labels;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Button>> _buttons;
    std::vector<std::string> _files;

    int _pending_index = -1;
    bool _pending_back = false;
};

/**
 * @brief App home: start a class, sync, Wi-Fi setup, cue mode.
 *
 */
class AquaMenuPage {
public:
    struct Status {
        size_t routineCount    = 0;
        bool wifiConfigured    = false;
        bool muted             = false;
        model::CueMode cueMode = model::CueMode::SoundAndBuzz;
    };

    AquaMenuPage(const Status& status);
    ~AquaMenuPage();

    std::function<void()> onStart;
    std::function<void()> onSync;
    std::function<void()> onWifiSetup;
    std::function<void()> onToggleCueMode;

    void update();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Label>> _labels;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Button>> _buttons;
    std::vector<std::function<void()>*> _actions;

    int _pending_index = -1;

    void addLabel(int y, const std::string& text, const lv_font_t* font, uint32_t color);
    void addButton(int y, const std::string& text, std::function<void()>* action, uint32_t color);
};

/**
 * @brief Full-screen progress page for a blocking operation (sync, AP setup).
 *
 */
class StatusPage {
public:
    StatusPage(const std::string& title);
    ~StatusPage();

    void setTitle(const std::string& title);
    void setMessage(const std::string& message);
    /** @brief Show a dismiss button; the callback fires when it is tapped. */
    void setDismissable(const std::string& buttonText, std::function<void()> onDismiss);

    void update();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_message;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_dismiss;

    std::function<void()> _on_dismiss;
    bool _pending_dismiss = false;
};

}  // namespace view
