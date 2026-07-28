/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <mooncake_log.h>
#include <M5GFX.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <atomic>
#include <memory>

static const std::string_view _tag = "hal_display";

static std::unique_ptr<M5GFX> _display;
static std::unique_ptr<LGFX_Sprite> _canvas;

/* -------------------------------------------------------------------------- */
/*                                   Display                                  */
/* -------------------------------------------------------------------------- */

void Hal::display_init()
{
    mclog::tagInfo(_tag, "display init");

    _display = std::make_unique<M5GFX>();
    if (!_display->init()) {
        mclog::tagError(_tag, "display init failed");
        _display.reset();
        return;
    }

    // Rotation 0 is 540x960 portrait on the M5Paper (panel is mounted
    // landscape-native with offset_rotation 3 in M5GFX's autodetect).
    _display->setRotation(0);
    // Steady-state mode: fast greyscale partial updates without the full
    // black/white flash. Quality refreshes are requested explicitly.
    _display->setEpdMode(epd_mode_t::epd_fast);
    _display->fillScreen(TFT_WHITE);

    mclog::tagInfo(_tag, "panel {}x{}", _display->width(), _display->height());
}

LGFX_Device& Hal::getDisplay()
{
    return *_display;
}

LGFX_Sprite& Hal::getCanvas()
{
    if (!_canvas) {
        // Canvas is currently unused on M5Paper, but the HAL API exposes it.
        // Allocate lazily if a caller requests it.
        _canvas = std::make_unique<LGFX_Sprite>(_display.get());
        _canvas->setPsram(true);
        if (!_canvas->createSprite(_display->width(), _display->height())) {
            mclog::tagError(_tag, "canvas init failed; falling back to 1x1");
            _canvas = std::make_unique<LGFX_Sprite>(_display.get());
            (void)_canvas->createSprite(1, 1);
        }
    }
    return *_canvas;
}

void Hal::updateCanvas()
{
    if (_canvas) {
        _canvas->pushSprite(0, 0);
    }
}

void Hal::setBackLightBrightness(int brightness, bool saveToSettings)
{
    // E-ink has no backlight; keep the setting plumbing so shared UI code works
    _bl_brightness = uitk::clamp(brightness, 0, 100);
    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("bl_lev", _bl_brightness);
    }
}

int Hal::getBackLightBrightness(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _bl_brightness = settings.GetInt("bl_lev", 80);
    }
    return _bl_brightness;
}

/* -------------------------------------------------------------------------- */
/*                                  Touchpad                                  */
/* -------------------------------------------------------------------------- */

Hal::TouchPoint Hal::getTouchPoint()
{
    Hal::TouchPoint point;
    if (!_display) {
        return point;
    }

    lgfx::touch_point_t tp;
    const int count = _display->getTouch(&tp, 1);
    if (count > 0) {
        point.num = count;
        point.x   = tp.x;
        point.y   = tp.y;
    }
    return point;
}

/* -------------------------------------------------------------------------- */
/*                                    Lvgl                                    */
/* -------------------------------------------------------------------------- */
#include <lvgl.h>

static SemaphoreHandle_t xGuiSemaphore;
static std::atomic<bool> _lvgl_update_enabled  = false;
static std::atomic<bool> _full_refresh_pending = false;

#define LV_BUFFER_LINE 120
// E-ink cannot follow the 10 ms cadence the AMOLED build uses; the panel needs
// >100 ms per partial update anyway, and the views only invalidate ~1x/sec.
#define LVGL_TASK_PERIOD_MS 50

static void lvgl_tick_timer(void* arg)
{
    (void)arg;
    lv_tick_inc(10);
}

static void lvgl_rtos_task(void* pvParameter)
{
    (void)pvParameter;
    while (1) {
        if (_lvgl_update_enabled && pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_timer_handler();
            if (_full_refresh_pending.exchange(false)) {
                // Re-show the panel's own framebuffer in quality mode: one
                // full black/white cycle that clears ghosting and doubles as
                // the interval-change flash cue. No LVGL redraw involved.
                _display->setEpdMode(epd_mode_t::epd_quality);
                _display->display();
                _display->setEpdMode(epd_mode_t::epd_fast);
            }
            xSemaphoreGive(xGuiSemaphore);
        }
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS));
    }
}

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    M5GFX& gfx = *(M5GFX*)lv_display_get_driver_data(disp);

    const uint32_t w = (area->x2 - area->x1 + 1);
    const uint32_t h = (area->y2 - area->y1 + 1);

    // pushImage lets Panel_IT8951 do its own RGB565 -> 16-grey conversion and
    // area update; writePixels would take a much slower path on this panel.
    gfx.startWrite();
    gfx.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t*)px_map);
    gfx.endWrite();

    lv_display_flush_ready(disp);
}

static void lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    (void)indev;
    auto tp = GetHAL().getTouchPoint();
    if (tp.num == 0) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = tp.x;
        data->point.y = tp.y;
    }
}

void Hal::lvgl_init()
{
    mclog::tagInfo(_tag, "lvgl init");

    lv_init();

    static lv_display_t* disp = lv_display_create(_display->width(), _display->height());
    if (disp == NULL) {
        mclog::tagError(_tag, "lv_display_create failed");
        return;
    }

    lv_display_set_driver_data(disp, _display.get());
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    const size_t buf_size = _display->width() * LV_BUFFER_LINE * sizeof(lv_color16_t);
    static uint8_t* buf1  = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    static uint8_t* buf2  = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    lv_display_set_buffers(disp, (void*)buf1, (void*)buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lvTouchpad = lv_indev_create();
    LV_ASSERT_MALLOC(lvTouchpad);
    if (lvTouchpad == NULL) {
        mclog::tagError(_tag, "lv_indev_create failed");
        return;
    }
    lv_indev_set_driver_data(lvTouchpad, _display.get());
    lv_indev_set_type(lvTouchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvTouchpad, lvgl_read_cb);
    lv_indev_set_display(lvTouchpad, disp);

    xGuiSemaphore                                     = xSemaphoreCreateMutex();
    const esp_timer_create_args_t periodic_timer_args = {.callback = &lvgl_tick_timer, .name = "lvgl_tick_timer"};
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 10 * 1000));
    xTaskCreate(lvgl_rtos_task, "lvgl_rtos_task", 4096 * 4, NULL, 1, NULL);

    startLvglUpdate();

    {
        LvglLockGuard lock;
        uitk::lvgl_cpp::ScreenActive screen;
        screen.setBgColor(lv_color_white());
    }
}

bool Hal::lvglLock()
{
    return xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE ? true : false;
}

void Hal::lvglUnlock()
{
    xSemaphoreGive(xGuiSemaphore);
}

void Hal::startLvglUpdate()
{
    _lvgl_update_enabled = true;
}

void Hal::stopLvglUpdate()
{
    _lvgl_update_enabled = false;
}

void Hal::requestEpdFullRefresh()
{
    _full_refresh_pending = true;
}
