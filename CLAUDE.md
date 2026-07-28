# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`StopWatch-UserDemo` — factory/evaluation firmware for the **M5Stack StopWatch**, a round-AMOLED wearable built on **ESP32-S3**. It is not a single stopwatch app but a small launcher-based OS hosting ~10 apps (watch face, stopwatch, alarm, badge, IMU/FFT demos, aqua timer, setup…).

Hardware: ESP32-S3, 16 MB flash, octal PSRAM @ 80 MHz. 480×480 round AMOLED (`CO5300`, QSPI) with a **466 px usable circle** — all UI containers are sized 466×466. Touch CST820, RTC RX8130, IMU BMI270+BMM150, PMIC M5PM1, IO expander M5IOE1, vibration motor, mic+speaker via `esp_codec_dev`, two physical buttons (btnA/btnB).

## Boards

Two build targets share this repo, keyed on `IDF_TARGET`:

- **esp32s3 (default)** — the StopWatch wearable, all apps.
- **esp32 — M5Paper v1.1** (4.7" 540×960 e-ink, IT8951 over SPI, GT911 touch, side wheel, **no speaker/buzzer/vibration**). Builds the **Aqua Timer only**: no launcher, `main.cpp` boots straight into the app and reopens it if closed. Cues are visual — interval type flips page polarity (Work/Power = inverted), and every timeline move triggers `requestEpdFullRefresh()` whose quality-mode flash is the cue *and* the ghosting cleaner. Sources split: `hal/boards/stopwatch/` vs `hal/boards/m5paper/`, `app_aqua_timer/view/` vs `view_epd/` (same class API, chosen in [main/CMakeLists.txt](main/CMakeLists.txt) + `#if BOARD_M5PAPER`). S3-only vendored components are excluded via `EXCLUDE_COMPONENTS` in the root CMakeLists; managed components use per-target `rules:` in the manifest. Per-target kconfig lives in `sdkconfig.defaults.esp32{,s3}` stacked on the shared `sdkconfig.defaults`.

## Commands

```bash
python3 ./fetch_repos.py     # REQUIRED first — git-clones components/ from repos.json + applies patches/
. $IDF_PATH/export.sh        # ESP-IDF v5.5.4 (exact version)
idf.py build                 # StopWatch (esp32s3, default)
idf.py flash monitor         # device-only; logs via idf.py monitor

# M5Paper: own build dir + own sdkconfig so the two targets never fight over ./sdkconfig
idf.py -B build.m5paper -DIDF_TARGET=esp32 -DSDKCONFIG=sdkconfig.m5paper build
idf.py -B build.m5paper -DSDKCONFIG=sdkconfig.m5paper flash monitor
```

- **Adding a new source file** under `apps/`, `assets/`, or `hal/` is auto-registered by the recursive globs in [main/CMakeLists.txt](main/CMakeLists.txt) — but CMake must reconfigure to pick it up (editing any `CMakeLists.txt`, or `idf.py fullclean`, forces this). Embedded assets (HTML/binary) are **not** globbed; add them to `EMBED_TXTFILES`/`EMBED_FILES` explicitly.
- **No test suite, no host/simulator build.** Verification is on-device via `mclog::tagInfo`/monitor. CI builds both targets (matrix in `.github/workflows/build.yml`) and runs a clang-format check. Pure-logic modules can be exercised with a host `g++ -std=c++17` harness against STL-only code (this is how the aqua-timer parser/runner were validated).
- **Formatting is enforced.** CI runs **clang-format 22** (`.clang-format`, Google-based, 120 col) over everything except `assets/` and `hal/drivers/`. Run `clang-format -i --style=file <files>` before committing or CI fails. `pip install clang-format` gives the right version.

## Dependencies

Two mechanisms, both needed:
- **`repos.json` + `fetch_repos.py`** → git-clones into `components/`: mooncake (app framework), mooncake_log (fmt-based logging), smooth_ui_toolkit (LVGL C++ wrapper + animation), M5GFX, LVGL 9.5, ArduinoJson (vendored but currently **unused** — JSON is hand-built), M5IOE1/M5PM1/BMI270 (with local `patches/`).
- **IDF component manager** ([main/idf_component.yml](main/idf_component.yml)): `esp_codec_dev`, `i2c_bus`, `esp-dsp`, and `78/esp-wifi-connect` (supplies `DnsServer`; its STA helpers are available but the firmware talks to `esp_wifi` directly).

## Architecture

### App model (mooncake)

Apps are `mooncake::AppAbility` subclasses with a **polled** lifecycle: `onCreate` (install) → `onOpen` (build UI) → `onRunning` (called every loop tick) → `onClose` (tear down). There are no timers or callbacks for logic — everything advances by comparing `GetHAL().millis()` inside `onRunning`. The main loop in [main/main.cpp](main/main.cpp) just calls `GetHAL().feedTheDog()` + `GetMooncake().update()` forever; apps are installed there and the launcher builds its icon ring from their `AppProps_t`.

**To add an app:** copy [main/apps/app_template/](main/apps/app_template/), register the include in [main/apps/apps.h](main/apps/apps.h) and an `installApp(...)` line in `main.cpp`. Convention per app: `app_x/app_x.{h,cpp}` (lifecycle) + `app_x/view/` (LVGL UI) + `app_x/model/` (logic). Shared code lives in [main/apps/common/](main/apps/common/) (`key_manager`, `arc_top_clock`, `status_bar`, `loading_page`, `audio`).

### HAL singleton

Everything hardware-facing goes through `GetHAL()` (declared in [main/hal/hal.h](main/hal/hal.h), split across `hal_*.cpp`): `millis`/`delay`, `vibrate` (async), `audioPlay` (async), `getSpeakerVolume`, `updateButtonStates` + `btnA`/`btnB`, RTC time, brightness, and the LVGL lock.

### LVGL threading — the critical rule

**LVGL runs in its own FreeRTOS task** (`lvgl_rtos_task`). Every LVGL access from app code (which runs on the main task) **must** hold the lock: use `LvglLockGuard lock;` (RAII, in `hal.h`) or `GetHAL().lvglLock()/lvglUnlock()`. Constructing, mutating, or destroying any widget without it is a race. UI uses LVGL 9.5 via `smooth_ui_toolkit`'s C++ wrappers (`uitk::lvgl_cpp::{Container,Label,Button,Image,Roller,…}`); raw `lv_*` C calls are freely mixed where the wrapper lacks coverage.

**Deferred-dispatch pattern:** a widget's click callback must not tear down the page that owns it. The established idiom (see `SelectMenuPage`, and the aqua-timer pages) is for callbacks to only set a pending flag/index, then act on it *after* the page's `update()` returns.

### Navigation & input

Universal, implemented identically in every app's `onRunning`: **hold btnA+btnB → `KeyEvent::GoHome` → `close()`**; btnA click → `GoPrevious`; btnB click → `GoNext`. Use `input::KeyManager` ([main/apps/common/key_manager/](main/apps/common/key_manager/)); the GoHome gesture must never be shadowed by other single-button holds. Touch (CST820) drives normal LVGL click signals. The standard order is `GetHAL().updateButtonStates()` once, then `_key_manager->update(false)`.

### Storage

- **NVS via `Settings`** ([main/hal/utils/settings/settings.h](main/hal/utils/settings/settings.h)): namespaced `GetString/SetString/GetInt/GetBlob/…`. Used for brightness, volume, timezone, alarms, etc.
- **FATFS + wear-levelling** on the 4 MB `storage` partition, mounted at **`/spiflash`** (`Hal::fs_init` → `wear_levelling_init`). Plain POSIX `fopen/mkdir/rename` works; copyable helpers and the atomic `.tmp`→`rename` write pattern are in [main/hal/hal_badge.cpp](main/hal/hal_badge.cpp). LVGL's stdio FS is mounted at drive letter `A` (`A:/spiflash/...`).

Note the `storage` partition is **shared** across apps (badge images up to 2 MB each, aqua-timer routine CSVs) — check free space before large writes.

### Networking

There is **no persistent connectivity**. Wi-Fi is brought up on demand:
- **SoftAP + captive portal** for local config: badge image upload ([main/hal/utils/config_ap/](main/hal/utils/config_ap/)) and aqua-timer Wi-Fi/repo setup ([main/hal/utils/net/wifi_config_ap.cpp](main/hal/utils/net/wifi_config_ap.cpp)). Both serve an embedded HTML page and block until the browser POSTs `/close`.
- **STA mode + HTTPS** ([main/hal/utils/net/](main/hal/utils/net/)) for the aqua-timer's GitHub sync (`wifi_sta`, `http_get` with the mbedTLS cert bundle).
- **All Wi-Fi paths share one initializer**, `net::ap_core::ensureWifiStackReady()` — initializing the stack twice returns `ESP_ERR_INVALID_STATE` and wedges Wi-Fi. Never route around it, and never `esp_netif_destroy` the AP/STA netifs (they are created-once statics).

Blocking network operations run from `onRunning` with the LVGL lock **released** (so the render task keeps drawing a status page); progress callbacks re-take the lock only to update text. TLS work is pushed onto a dedicated large-stack FreeRTOS task because the main task stack (8192) is too small for the mbedTLS handshake.

### Assets & fonts

Icons and fonts are LVGL C arrays under [main/assets/](main/assets/), declared in [main/assets/assets.h](main/assets/assets.h). Available fonts: Maple Mono 24/28/48, CommissionerMedium 64/108, MontserratSemiBold26, plus LVGL Montserrat 10/16/18/22/28/36 (enabled in `sdkconfig.defaults` — adding a size means adding a `CONFIG_LV_FONT_MONTSERRAT_*` there).

## Conventions

- Log with fmtlib style: `mclog::tagInfo(tag, "fmt {}", x)` (also `tagWarn`/`tagError`).
- SPDX header on every new source file (see any existing file).
- `sdkconfig.defaults` records only non-default symbols (it is generated by `idf.py save-defconfig`, which drops defaults) — hand-added lines that pin a default get a comment noting save-defconfig will remove them.
