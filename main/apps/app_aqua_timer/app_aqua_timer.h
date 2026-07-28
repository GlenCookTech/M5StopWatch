/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "model/config.h"
#include "model/runner.h"
// Same view API, two implementations: round AMOLED vs 540x960 e-ink
#if BOARD_M5PAPER
#include "view_epd/view.h"
#else
#include "view/view.h"
#endif

#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>

/**
 * @brief Aqua Timer — runs interval routines authored as CSV in a GitHub repo,
 * synced over Wi-Fi and cached to flash, for coaching aqua-aerobics classes.
 *
 */
class AppAquaTimer : public mooncake::AppAbility {
public:
    AppAquaTimer();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class Page { Menu, Picker, Running };

    Page _page = Page::Menu;

    model::AquaConfig _config;
    std::unique_ptr<model::RoutineRunner> _runner;
    std::unique_ptr<input::KeyManager> _key_manager;

    std::unique_ptr<view::AquaMenuPage> _menu;
    std::unique_ptr<view::RoutinePickerPage> _picker;
    std::unique_ptr<view::RunningPage> _running;

    // Actions are deferred out of LVGL click callbacks: a callback may ask to
    // replace the very page that owns it, or kick off a blocking operation that
    // must run outside the LVGL lock.
    Page _pending_page        = Page::Menu;
    bool _page_change_pending = false;
    std::string _pending_routine_file;
    bool _pending_sync       = false;
    bool _pending_wifi_setup = false;
    bool _pending_cue_toggle = false;

    void showMenu();
    void showPicker();
    void startRoutine(const std::string& file);
    void applyPendingActions();

    void runSync();
    void runWifiSetup();
    void toggleCueMode();
};
