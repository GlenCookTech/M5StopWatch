/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>

using namespace view;
using namespace uitk::lvgl_cpp;

AquaMenuPage::AquaMenuPage(const Status& status)
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setSize(540, 960);
    _panel->setBgColor(lv_color_white());
    _panel->setPadding(0, 0, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setRadius(0);
    _panel->setScrollDir(LV_DIR_VER);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);

    int cursor_y = 80;

    addLabel(cursor_y, "Aqua Timer", &lv_font_montserrat_48, 0x000000);
    cursor_y += 60 + 60;

    // Start leads the menu, but is inert with an empty cache — the label says why
    if (status.routineCount > 0) {
        addButton(cursor_y, "Start a Class", &onStart, true);
    } else {
        addButton(cursor_y, "No routines yet", nullptr, false);
    }
    cursor_y += 130 + 40;

    const std::string sync_label = status.wifiConfigured ? "Sync Routines" : "Sync (set up Wi-Fi first)";
    addButton(cursor_y, sync_label, status.wifiConfigured ? &onSync : &onWifiSetup, false);
    cursor_y += 130 + 40;

    addButton(cursor_y, "Wi-Fi Setup", &onWifiSetup, false);
    cursor_y += 130 + 40;

    // No speaker, buzzer or motor on this board: cues are always visual, so
    // the AMOLED build's cue-mode toggle becomes a plain statement of fact.
    addLabel(cursor_y, "Cues: screen flash on interval change", &lv_font_montserrat_22, 0x555555);
    cursor_y += 40;
}

AquaMenuPage::~AquaMenuPage()
{
    _buttons.clear();
    _labels.clear();
    _actions.clear();
    _panel.reset();
}

void AquaMenuPage::addLabel(int y, const std::string& text, const lv_font_t* font, uint32_t color)
{
    auto label = std::make_unique<Label>(*_panel);
    label->setText(text);
    label->setTextFont(font);
    label->setTextColor(lv_color_hex(color));
    label->align(LV_ALIGN_TOP_MID, 0, y);
    _labels.push_back(std::move(label));
}

void AquaMenuPage::addButton(int y, const std::string& text, std::function<void()>* action, bool primary)
{
    auto btn = std::make_unique<Button>(*_panel);
    btn->setSize(480, 130);
    btn->align(LV_ALIGN_TOP_MID, 0, y);
    btn->setRadius(8);
    btn->setShadowWidth(0);

    if (primary) {
        // Primary action: solid black, white text
        btn->setBgColor(lv_color_black());
        btn->setBorderWidth(0);
        btn->label().setTextColor(lv_color_white());
    } else {
        // Secondary: white with black border; disabled: grey border and text
        btn->setBgColor(lv_color_white());
        btn->setBorderWidth(3);
        btn->setBorderColor(lv_color_hex(action ? 0x000000 : 0x999999));
        btn->label().setTextColor(lv_color_hex(action ? 0x000000 : 0x999999));
    }

    btn->label().setText(text);
    btn->label().setTextFont(&lv_font_montserrat_36);
    btn->label().align(LV_ALIGN_CENTER, 0, 0);
    btn->label().setWidth(420);
    btn->label().setTextAlign(LV_TEXT_ALIGN_CENTER);
    btn->label().setLongMode(LV_LABEL_LONG_MODE_DOTS);

    if (action) {
        const int index = static_cast<int>(_actions.size());
        btn->onClick().connect([this, index]() { _pending_index = index; });
        _actions.push_back(action);
    }
    _buttons.push_back(std::move(btn));
}

void AquaMenuPage::update()
{
    if (_pending_index >= 0 && _pending_index < static_cast<int>(_actions.size())) {
        auto* action   = _actions[_pending_index];
        _pending_index = -1;
        if (action && *action) (*action)();
    }
}
