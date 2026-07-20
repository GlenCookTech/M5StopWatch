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
    _panel->setSize(466, 466);
    _panel->setBgColor(lv_color_black());
    _panel->setPadding(0, 72, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setRadius(0);
    _panel->setScrollDir(LV_DIR_VER);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

    int cursor_y = 40;

    addLabel(cursor_y, "Aqua Timer", &MontserratSemiBold26, 0xFFFFFF);
    cursor_y += 28 + 26;

    // Start leads the menu, but is inert with an empty cache — the label says why
    if (status.routineCount > 0) {
        addButton(cursor_y, "Start a Class", &onStart, 0x1E4C3C);
    } else {
        addButton(cursor_y, "No routines yet", nullptr, 0x2A2A2A);
    }
    cursor_y += 119 + 21;

    const std::string sync_label = status.wifiConfigured ? "Sync Routines" : "Sync (set up Wi-Fi first)";
    addButton(cursor_y, sync_label, status.wifiConfigured ? &onSync : &onWifiSetup, 0x2C4A66);
    cursor_y += 119 + 21;

    addButton(cursor_y, "Wi-Fi Setup", &onWifiSetup, 0x3A3A3A);
    cursor_y += 119 + 21;

    const std::string cue_label = status.cueMode == model::CueMode::BuzzOnly ? "Cues: Buzz only" : "Cues: Sound + Buzz";
    addButton(cursor_y, cue_label, &onToggleCueMode, 0x3A3A3A);
    cursor_y += 119 + 21;

    if (status.muted && status.cueMode != model::CueMode::BuzzOnly) {
        addLabel(cursor_y, "Speaker is muted", &lv_font_montserrat_18, 0xFFC46B);
        cursor_y += 30;
    }
    cursor_y += 20;
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

void AquaMenuPage::addButton(int y, const std::string& text, std::function<void()>* action, uint32_t color)
{
    auto btn = std::make_unique<Button>(*_panel);
    btn->setSize(374, 119);
    btn->align(LV_ALIGN_TOP_MID, 0, y);
    btn->setBgColor(lv_color_hex(color));
    btn->setBorderWidth(0);
    btn->setShadowWidth(0);
    btn->setRadius(60);

    btn->label().setText(text);
    btn->label().setTextFont(&lv_font_montserrat_28);
    btn->label().setTextColor(lv_color_hex(action ? 0xFFFFFF : 0x777777));
    btn->label().align(LV_ALIGN_CENTER, 0, 0);
    btn->label().setWidth(300);
    btn->label().setTextAlign(LV_TEXT_ALIGN_CENTER);
    btn->label().setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

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
