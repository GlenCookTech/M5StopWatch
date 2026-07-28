#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#
# Provision a stock Ubuntu machine to build StopWatch-UserDemo.
#
# Installs, in order:
#   1. APT build prerequisites (the ESP-IDF list + this project's extras)
#   2. ESP-IDF (pinned version) and its cross-toolchains
#   3. clang-format, pinned to the version CI enforces
#   4. This project's components/ via fetch_repos.py
#   5. A verification build (skip with --no-build)
#
# Safe to re-run: every step is idempotent.
#
# Usage:
#   ./setup-ubuntu.sh [--no-build] [--targets esp32s3,esp32] [--idf-version v5.5.4]
#
# Env overrides:
#   IDF_VERSION   ESP-IDF git tag to install          (default: v5.5.4)
#   IDF_TARGETS   comma-separated chips to install    (default: esp32s3,esp32)
#   IDF_BASE_DIR  where esp-idf is cloned             (default: $HOME/esp)
#   IDF_TOOLS_PATH  where toolchains land             (default: $HOME/.espressif)

set -euo pipefail

# --- Configuration -----------------------------------------------------------

# v5.5.4 exactly: the project is pinned to it (see CLAUDE.md and the CI
# workflow's esp_idf_version). Other 5.5.x releases are untested here.
IDF_VERSION="${IDF_VERSION:-v5.5.4}"

# esp32s3 = the StopWatch itself. esp32 = M5Paper, whose port is driven by
# sdkconfig.defaults.esp32. Drop to just "esp32s3" to save a toolchain download
# if you never build for the Paper.
IDF_TARGETS="${IDF_TARGETS:-esp32s3,esp32}"

# CI enforces clang-format 22 (see .github/workflows/clang-format-check.yml).
# A different major version reformats differently and will fail the check.
CLANG_FORMAT_VERSION="${CLANG_FORMAT_VERSION:-22}"

IDF_BASE_DIR="${IDF_BASE_DIR:-$HOME/esp}"
IDF_PATH_TARGET="$IDF_BASE_DIR/esp-idf"
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$HOME/.espressif}"

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_BUILD=1

# --- Argument parsing --------------------------------------------------------

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) RUN_BUILD=0; shift ;;
        --targets)
            [[ $# -ge 2 ]] || die "--targets requires a value (e.g. esp32s3,esp32)"
            IDF_TARGETS="$2"
            shift 2
            ;;
        --idf-version)
            [[ $# -ge 2 ]] || die "--idf-version requires a value (e.g. v5.5.4)"
            IDF_VERSION="$2"
            shift 2
            ;;
        -h|--help)       sed -n '7,26s/^# \?//p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *)               echo "Unknown option: $1 (try --help)" >&2; exit 2 ;;
    esac
done

log()  { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33mwarning: %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[1;31merror: %s\033[0m\n' "$*" >&2; exit 1; }

# --- Preflight ---------------------------------------------------------------

[[ "$(uname -s)" == "Linux" ]] || die "this script targets Linux (Ubuntu/Debian)"
command -v apt-get >/dev/null || die "apt-get not found; this script is for Ubuntu/Debian"

# ESP-IDF's install.sh writes into $HOME and refuses to behave sensibly when the
# whole thing runs as root, so require a normal user with sudo instead.
if [[ "$(id -u)" -eq 0 ]]; then
    die "run as a normal user (the script calls sudo only for apt), not as root"
fi

if sudo -n true 2>/dev/null; then :; else
    log "sudo access is needed for package installation"
    sudo -v || die "cannot obtain sudo"
fi

# ESP-IDF plus two toolchains is several GB; failing here beats failing halfway
# through a toolchain extraction with a confusing error.
avail_gb=$(df -BG --output=avail "$HOME" | tail -1 | tr -dc '0-9')
if [[ "$avail_gb" -lt 10 ]]; then
    warn "only ${avail_gb}G free in $HOME; ESP-IDF + toolchains need roughly 8-10G"
fi

[[ -f "$PROJECT_DIR/fetch_repos.py" ]] || \
    die "run this from inside the StopWatch-UserDemo checkout (no fetch_repos.py beside the script)"

# --- 1. APT prerequisites ----------------------------------------------------

log "Installing APT prerequisites"

# The first six are ESP-IDF's documented Linux requirements. dfu-util and
# libusb are needed for flashing over USB; ccache is optional but cuts rebuild
# times substantially and ESP-IDF picks it up automatically when present.
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    git wget curl ca-certificates \
    flex bison gperf \
    cmake ninja-build ccache \
    libffi-dev libssl-dev \
    dfu-util libusb-1.0-0 \
    python3 python3-pip python3-venv

# --- 2. ESP-IDF --------------------------------------------------------------

log "Installing ESP-IDF $IDF_VERSION (targets: $IDF_TARGETS)"

mkdir -p "$IDF_BASE_DIR"

if [[ -d "$IDF_PATH_TARGET/.git" ]]; then
    current="$(git -C "$IDF_PATH_TARGET" describe --tags --exact-match 2>/dev/null || echo unknown)"
    if [[ "$current" == "$IDF_VERSION" ]]; then
        echo "ESP-IDF $IDF_VERSION already present at $IDF_PATH_TARGET"
    else
        warn "$IDF_PATH_TARGET is at '$current', wanted '$IDF_VERSION'"
        warn "delete it and re-run, or set IDF_BASE_DIR to install alongside it"
        die "refusing to modify an existing ESP-IDF checkout"
    fi
else
    # Shallow clone: a full ESP-IDF history is ~1GB of objects nobody needs to
    # build a tagged release.
    git clone --branch "$IDF_VERSION" --depth 1 --shallow-submodules \
        --recursive https://github.com/espressif/esp-idf.git "$IDF_PATH_TARGET"
fi

export IDF_PATH="$IDF_PATH_TARGET"

# install.sh is idempotent: already-present tools are detected and skipped.
"$IDF_PATH/install.sh" "$IDF_TARGETS"

# --- 3. clang-format ---------------------------------------------------------

log "Installing clang-format $CLANG_FORMAT_VERSION"

# Ubuntu 24.04+ marks the system Python as externally managed (PEP 668), so a
# plain `pip install --user` is refused. An isolated venv sidesteps that without
# needing --break-system-packages.
CF_VENV="$IDF_TOOLS_PATH/clang-format-venv"
if [[ ! -x "$CF_VENV/bin/clang-format" ]]; then
    python3 -m venv "$CF_VENV"
    "$CF_VENV/bin/pip" install --quiet --upgrade pip
    # The PyPI 'clang-format' package tracks LLVM versioning, so 22.* is the
    # clang-format 22 the CI action pins. Fall back to latest if that series is
    # unavailable, and let the version check below flag any mismatch.
    "$CF_VENV/bin/pip" install --quiet "clang-format~=${CLANG_FORMAT_VERSION}.0" \
        || "$CF_VENV/bin/pip" install --quiet clang-format
fi

mkdir -p "$HOME/.local/bin"
ln -sf "$CF_VENV/bin/clang-format" "$HOME/.local/bin/clang-format"

cf_actual="$("$CF_VENV/bin/clang-format" --version | grep -oE '[0-9]+' | head -1)"
if [[ "$cf_actual" != "$CLANG_FORMAT_VERSION" ]]; then
    warn "installed clang-format is version $cf_actual, but CI enforces $CLANG_FORMAT_VERSION"
    warn "formatting locally may not match the CI check"
fi

# --- 4. Serial port access ---------------------------------------------------

# Flashing needs read/write on /dev/ttyACM* and /dev/ttyUSB*, which belong to
# the dialout group. Harmless on a headless build box that never flashes.
if ! id -nG "$USER" | grep -qw dialout; then
    log "Adding $USER to the dialout group (for flashing over USB)"
    sudo usermod -aG dialout "$USER"
    NEEDS_RELOGIN=1
fi

# --- 5. Project dependencies -------------------------------------------------

log "Fetching project components (fetch_repos.py)"

# Must run before the first build: components/ is .gitignore'd and populated
# entirely by this script, which also applies everything under patches/.
cd "$PROJECT_DIR"
python3 ./fetch_repos.py

# --- 6. Verification build ---------------------------------------------------

if [[ "$RUN_BUILD" -eq 1 ]]; then
    log "Running a verification build (esp32s3)"

    # shellcheck disable=SC1091
    source "$IDF_PATH/export.sh"

    # Only set the target when there is no sdkconfig yet; re-running set-target
    # discards any local menuconfig changes.
    if [[ ! -f "$PROJECT_DIR/sdkconfig" ]]; then
        idf.py set-target esp32s3
    fi

    idf.py build
    log "Build succeeded"
else
    log "Skipping verification build (--no-build)"
fi

# --- Done --------------------------------------------------------------------

cat <<EOF

$(printf '\033[1;32mSetup complete.\033[0m')

Activate ESP-IDF in each new shell:

    . "$IDF_PATH/export.sh"

Then build and flash:

    idf.py build                    # StopWatch (esp32s3)
    idf.py flash monitor            # add -p /dev/ttyACM0 if autodetect fails

To make activation automatic:

    echo '. "$IDF_PATH/export.sh" >/dev/null' >> ~/.bashrc

Ensure ~/.local/bin is on PATH so the pinned clang-format is found:

    export PATH="\$HOME/.local/bin:\$PATH"
    clang-format -i --style=file <files>   # run before committing; CI enforces it
EOF

if [[ -n "${NEEDS_RELOGIN:-}" ]]; then
    printf '\n\033[1;33mLog out and back in for dialout group membership to take effect.\033[0m\n'
fi
