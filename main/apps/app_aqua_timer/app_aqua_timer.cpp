/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_aqua_timer.h"
#include "model/routine_store.h"
#include "model/sync.h"

#include <assets/assets.h>
#include <hal/hal.h>
#include <hal/utils/net/wifi_config_ap.h>
#include <mooncake.h>
#include <mooncake_log.h>

using namespace mooncake;

AppAquaTimer::AppAquaTimer()
{
    setAppInfo().name = "Aqua Timer";
    // No dedicated icon yet — the launcher renders a text tile when icon is null
    // (app_launcher/view/view.cpp handles the nullptr case).
    setAppInfo().icon = (void*)&icon_stopwatch;
}

void AppAquaTimer::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppAquaTimer::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _config      = model::AquaConfig::load();
    _runner      = std::make_unique<model::RoutineRunner>();
    _key_manager = std::make_unique<input::KeyManager>();

    showMenu();
}

void AppAquaTimer::onRunning()
{
    GetHAL().updateButtonStates();
    const input::KeyEvent event = _key_manager ? _key_manager->update(false) : input::KeyEvent::None;

    // The two-button home gesture must always win, even mid-class
    if (event == input::KeyEvent::GoHome) {
        close();
        return;
    }

    // While a class runs, the physical buttons step through intervals. Elsewhere
    // the buttons are unused and navigation is by touch.
    if (_page == Page::Running && _running) {
        if (event == input::KeyEvent::GoPrevious) {
            _runner->previous();
        } else if (event == input::KeyEvent::GoNext) {
            _runner->next();
        }
    }

    {
        LvglLockGuard lock;
        if (_page == Page::Menu && _menu) {
            _menu->update();
        } else if (_page == Page::Picker && _picker) {
            _picker->update();
        } else if (_page == Page::Running && _running) {
            _running->update();
        }
    }

    // Deferred actions run after the page update, so a click handler that fired
    // during update() has already returned before we tear its page down.
    applyPendingActions();
}

void AppAquaTimer::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;
    _running.reset();
    _picker.reset();
    _menu.reset();
    _runner.reset();
}

/* -------------------------------------------------------------------------- */
/*                                   Pages                                    */
/* -------------------------------------------------------------------------- */

void AppAquaTimer::showMenu()
{
    LvglLockGuard lock;
    _running.reset();
    _picker.reset();
    _menu.reset();

    view::AquaMenuPage::Status status;
    status.routineCount   = model::store::listRoutines().size();
    status.wifiConfigured = _config.isConfigured();
    status.cueMode        = _config.cueMode;
    status.muted          = GetHAL().getSpeakerVolume() <= 0;

    _menu          = std::make_unique<view::AquaMenuPage>(status);
    _menu->onStart = [this]() {
        _pending_page        = Page::Picker;
        _page_change_pending = true;
    };
    _menu->onSync          = [this]() { _pending_sync = true; };
    _menu->onWifiSetup     = [this]() { _pending_wifi_setup = true; };
    _menu->onToggleCueMode = [this]() { _pending_cue_toggle = true; };

    _page = Page::Menu;
}

void AppAquaTimer::showPicker()
{
    LvglLockGuard lock;
    _running.reset();
    _menu.reset();
    _picker.reset();

    _picker                    = std::make_unique<view::RoutinePickerPage>(model::store::listRoutines());
    _picker->onRoutineSelected = [this](const std::string& file) {
        _pending_routine_file = file;
        _pending_page         = Page::Running;
        _page_change_pending  = true;
    };
    _picker->onBack = [this]() {
        _pending_page        = Page::Menu;
        _page_change_pending = true;
    };

    _page = Page::Picker;
}

void AppAquaTimer::startRoutine(const std::string& file)
{
    model::Routine routine;
    std::string error;
    const bool loaded = model::store::loadRoutine(file, routine, error);

    if (!loaded) {
        // Corrupt or missing cache entry — a rare edge case. Log and bounce back
        // to the menu rather than opening a broken running screen.
        mclog::tagError(getAppInfo().name, "load {} failed: {}", file, error);
        showMenu();
        return;
    }

    LvglLockGuard lock;
    _picker.reset();
    _menu.reset();
    _running.reset();

    _runner->cues().setMode(_config.cueMode);
    _runner->load(routine);
    _runner->start();

    _running         = std::make_unique<view::RunningPage>(_runner.get());
    _running->onExit = [this]() {
        _pending_page        = Page::Menu;
        _page_change_pending = true;
    };

    _page = Page::Running;
}

void AppAquaTimer::applyPendingActions()
{
    if (_pending_cue_toggle) {
        _pending_cue_toggle = false;
        toggleCueMode();
        return;
    }
    if (_pending_wifi_setup) {
        _pending_wifi_setup = false;
        runWifiSetup();
        return;
    }
    if (_pending_sync) {
        _pending_sync = false;
        runSync();
        return;
    }
    if (_page_change_pending) {
        _page_change_pending = false;
        switch (_pending_page) {
            case Page::Menu:
                showMenu();
                break;
            case Page::Picker:
                showPicker();
                break;
            case Page::Running:
                startRoutine(_pending_routine_file);
                break;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                              Blocking actions                             */
/* -------------------------------------------------------------------------- */

void AppAquaTimer::toggleCueMode()
{
    _config.cueMode =
        _config.cueMode == model::CueMode::BuzzOnly ? model::CueMode::SoundAndBuzz : model::CueMode::BuzzOnly;
    _config.save();
    showMenu();  // rebuild so the menu label reflects the new mode
}

void AppAquaTimer::runWifiSetup()
{
    // onRunning has already released the LVGL lock before calling us, so the
    // render task keeps drawing the status page while the portal blocks. The
    // progress callback grabs the lock only for the moment it updates text.
    std::unique_ptr<view::StatusPage> page;
    {
        LvglLockGuard lock;
        _menu.reset();
        page = std::make_unique<view::StatusPage>("Wi-Fi Setup");
        page->setMessage("Starting access point…");
    }

    net::wifi_config_ap::run(
        [&](std::string_view msg) {
            LvglLockGuard lock;
            page->setMessage(std::string(msg));
        },
        [this](const net::wifi_config_ap::Credentials& creds, std::string& message) -> bool {
            std::string url = creds.baseUrl;
            std::string error;
            if (!model::normalizeBaseUrl(url, error)) {
                message = error;
                return false;
            }
            if (creds.ssid.empty()) {
                message = "Wi-Fi network is required";
                return false;
            }
            _config.ssid     = creds.ssid;
            _config.password = creds.password;
            _config.baseUrl  = url;
            _config.save();
            message = "Saved. Tap Done, then Sync on the watch.";
            return true;
        });

    {
        LvglLockGuard lock;
        page.reset();
    }
    showMenu();
}

void AppAquaTimer::runSync()
{
    if (!_config.isConfigured()) {
        runWifiSetup();
        return;
    }

    std::unique_ptr<view::StatusPage> page;
    {
        LvglLockGuard lock;
        _menu.reset();
        page = std::make_unique<view::StatusPage>("Syncing routines");
        page->setMessage("Connecting…");
    }

    // Sync spawns its own worker task for TLS headroom, then blocks here. The
    // LVGL lock is already released (onRunning closed its guard before calling
    // us), so the worker's progress callbacks can take it as needed.
    const model::SyncResult result = model::syncRoutines(_config, [&](const std::string& msg) {
        LvglLockGuard lock;
        page->setMessage(msg);
    });

    {
        LvglLockGuard lock;
        if (result.ok) {
            std::string summary =
                std::to_string(result.downloaded) + " of " + std::to_string(result.total) + " routines ready";
            if (!result.failed.empty()) {
                summary += "\n" + std::to_string(result.failed.size()) + " could not be read";
            }
            page->setTitle("Sync complete");
            page->setMessage(summary);
        } else {
            page->setTitle("Sync failed");
            page->setMessage(result.error);
        }
        // Keep the result on screen until acknowledged
        page->setDismissable("Back", [this]() {
            _pending_page        = Page::Menu;
            _page_change_pending = true;
        });
    }

    // Drive the status page until the user dismisses it. onRunning is not being
    // called during this blocking action, so pump it locally.
    while (!_page_change_pending) {
        GetHAL().updateButtonStates();
        if (_key_manager && _key_manager->update(false) == input::KeyEvent::GoHome) {
            LvglLockGuard lock;
            page.reset();
            close();
            return;
        }
        {
            LvglLockGuard lock;
            page->update();
        }
        GetHAL().delay(16);
    }

    {
        LvglLockGuard lock;
        page.reset();
    }
    // _page_change_pending is set; let applyPendingActions in the next loop handle it
}
