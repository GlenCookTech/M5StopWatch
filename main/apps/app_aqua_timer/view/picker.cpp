/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <cstdio>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

std::string duration_hint(uint32_t seconds)
{
    if (seconds == 0) return {};
    char buffer[16] = {};
    snprintf(buffer, sizeof(buffer), "%u min", (seconds + 59) / 60);
    return std::string(buffer);
}

}  // namespace

RoutinePickerPage::RoutinePickerPage(const std::vector<model::RoutineSummary>& routines)
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

    auto title = std::make_unique<Label>(*_panel);
    title->setText(routines.empty() ? "No routines" : "Pick a class");
    title->setTextFont(&MontserratSemiBold26);
    title->setTextColor(lv_color_hex(0xFFFFFF));
    title->align(LV_ALIGN_TOP_MID, 0, cursor_y);
    _labels.push_back(std::move(title));
    cursor_y += 28 + 28;

    if (routines.empty()) {
        auto hint = std::make_unique<Label>(*_panel);
        hint->setText("Run Sync from the menu\nto download routines");
        hint->setTextFont(&lv_font_montserrat_22);
        hint->setTextColor(lv_color_hex(0x8A8A8A));
        hint->setTextAlign(LV_TEXT_ALIGN_CENTER);
        hint->align(LV_ALIGN_TOP_MID, 0, cursor_y);
        _labels.push_back(std::move(hint));
        cursor_y += 80;
    }

    for (size_t i = 0; i < routines.size(); ++i) {
        const auto& routine = routines[i];

        auto btn = std::make_unique<Button>(*_panel);
        btn->setSize(374, 119);
        btn->align(LV_ALIGN_TOP_MID, 0, cursor_y);
        btn->setBgColor(lv_color_hex(0x1E4C3C));
        btn->setBorderWidth(0);
        btn->setShadowWidth(0);
        btn->setRadius(60);

        std::string label      = routine.title;
        const std::string hint = duration_hint(routine.totalSeconds);
        if (!hint.empty()) label += "\n" + hint;

        btn->label().setText(label);
        btn->label().setTextFont(&lv_font_montserrat_28);
        btn->label().setTextColor(lv_color_hex(0xFFFFFF));
        btn->label().align(LV_ALIGN_CENTER, 0, 0);
        btn->label().setWidth(300);
        btn->label().setTextAlign(LV_TEXT_ALIGN_CENTER);
        btn->label().setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

        const int index = static_cast<int>(i);
        btn->onClick().connect([this, index]() { _pending_index = index; });

        _files.push_back(routine.file);
        _buttons.push_back(std::move(btn));
        cursor_y += 119 + 21;
    }

    auto back = std::make_unique<Button>(*_panel);
    back->setSize(374, 90);
    back->align(LV_ALIGN_TOP_MID, 0, cursor_y);
    back->setBgColor(lv_color_hex(0x3A3A3A));
    back->setBorderWidth(0);
    back->setShadowWidth(0);
    back->setRadius(45);
    back->label().setText("Back");
    back->label().setTextFont(&lv_font_montserrat_28);
    back->label().setTextColor(lv_color_hex(0xCCCCCC));
    back->onClick().connect([this]() { _pending_back = true; });
    _buttons.push_back(std::move(back));
    cursor_y += 110;
}

RoutinePickerPage::~RoutinePickerPage()
{
    _buttons.clear();
    _labels.clear();
    _panel.reset();
}

void RoutinePickerPage::update()
{
    // Dispatch outside the LVGL event callback, matching SelectMenuPage: the
    // click handler may tear down this very page.
    if (_pending_back) {
        _pending_back = false;
        if (onBack) onBack();
        return;
    }
    if (_pending_index >= 0 && _pending_index < static_cast<int>(_files.size())) {
        const std::string file = _files[_pending_index];
        _pending_index         = -1;
        if (onRoutineSelected) onRoutineSelected(file);
    }
}
