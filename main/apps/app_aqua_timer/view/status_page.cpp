/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>

using namespace view;
using namespace uitk::lvgl_cpp;

StatusPage::StatusPage(const std::string& title)
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setSize(466, 466);
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setBgColor(lv_color_black());
    _panel->setBorderWidth(0);
    _panel->setRadius(233);
    _panel->setPadding(0, 0, 0, 0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(_panel->get(), LV_OBJ_FLAG_SCROLLABLE);

    _label_title = std::make_unique<Label>(*_panel);
    _label_title->setTextFont(&MontserratSemiBold26);
    _label_title->setTextColor(lv_color_hex(0xFFFFFF));
    _label_title->setWidth(360);
    _label_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_title->setLongMode(LV_LABEL_LONG_MODE_WRAP);
    _label_title->setText(title);
    _label_title->align(LV_ALIGN_CENTER, 0, -70);

    _label_message = std::make_unique<Label>(*_panel);
    _label_message->setTextFont(&lv_font_montserrat_22);
    _label_message->setTextColor(lv_color_hex(0xB8C4CA));
    _label_message->setWidth(380);
    _label_message->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_message->setLongMode(LV_LABEL_LONG_MODE_WRAP);
    _label_message->setText("");
    _label_message->align(LV_ALIGN_CENTER, 0, 6);
}

StatusPage::~StatusPage()
{
    _btn_dismiss.reset();
    _label_message.reset();
    _label_title.reset();
    _panel.reset();
}

void StatusPage::setTitle(const std::string& title)
{
    _label_title->setText(title);
}

void StatusPage::setMessage(const std::string& message)
{
    _label_message->setText(message);
}

void StatusPage::setDismissable(const std::string& buttonText, std::function<void()> onDismiss)
{
    _on_dismiss = std::move(onDismiss);

    if (!_btn_dismiss) {
        _btn_dismiss = std::make_unique<Button>(*_panel);
        _btn_dismiss->setSize(200, 74);
        _btn_dismiss->setRadius(37);
        _btn_dismiss->setBorderWidth(0);
        _btn_dismiss->setShadowWidth(0);
        _btn_dismiss->setBgColor(lv_color_hex(0x3E4A52));
        _btn_dismiss->label().setTextFont(&lv_font_montserrat_28);
        _btn_dismiss->label().setTextColor(lv_color_hex(0xFFFFFF));
        _btn_dismiss->align(LV_ALIGN_CENTER, 0, 120);
        _btn_dismiss->onClick().connect([this]() { _pending_dismiss = true; });
    }
    _btn_dismiss->label().setText(buttonText);
    _btn_dismiss->setHidden(false);
}

void StatusPage::update()
{
    if (_pending_dismiss) {
        _pending_dismiss = false;
        if (_on_dismiss) _on_dismiss();
    }
}
