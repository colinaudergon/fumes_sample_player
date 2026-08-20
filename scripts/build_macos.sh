#!/usr/bin/env bash
#
# build_macos.sh - Installs dependencies and builds the native (host) sample_player target on
# macOS, using the same repo-root CMakeLists.txt as the Linux/WSL native build (see README.md's
# "Option A - Native build" section).
#
# Usage:
#   scripts/build_macos.sh [build_dir]
#
# build_dir defaults to "build-macos" (kept separate from the Linux/WSL "build/" directory since
# CMake caches are not portable across machines/OSes).
#
# What this script does:
#   1. Verifies Xcode Command Line Tools are installed (provides clang/clang++/make).
#   2. Installs Homebrew if missing.
#   3. Installs/updates the brew dependencies: cmake, qt6.
#   4. Configures and builds the project via CMake, pointing CMAKE_PREFIX_PATH at Homebrew's
#      Qt6 install so `find_package(Qt6 REQUIRED COMPONENTS Widgets)` (see
#      hw_interfaces/linux/qt_gui/CMakeLists.txt) can find it.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: this script is for macOS only (detected: $(uname -s))" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${1:-build-macos}"

echo "==> Checking for Xcode Command Line Tools..."
if ! xcode-select -p >/dev/null 2>&1; then
    echo "Xcode Command Line Tools not found. Triggering install (a GUI prompt will appear)..."
    xcode-select --install
    echo "error: re-run this script once the Command Line Tools install finishes." >&2
    exit 1
fi

echo "==> Checking for Homebrew..."
if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew not found. Installing it now..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

    # Homebrew installs to different prefixes on Apple Silicon (/opt/homebrew) vs Intel
    # (/usr/local); pick up whichever one the installer just created for the rest of this run.
    if [[ -x /opt/homebrew/bin/brew ]]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [[ -x /usr/local/bin/brew ]]; then
        eval "$(/usr/local/bin/brew shellenv)"
    fi
fi

echo "==> Installing/updating dependencies via Homebrew (cmake, qt6)..."
brew install cmake qt6

QT6_PREFIX="$(brew --prefix qt6)"
echo "==> Using Qt6 from: ${QT6_PREFIX}"

echo "==> Configuring CMake project (build dir: ${BUILD_DIR})..."
cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/${BUILD_DIR}" \
    -DCMAKE_PREFIX_PATH="${QT6_PREFIX}"

echo "==> Building..."
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
cmake --build "${REPO_ROOT}/${BUILD_DIR}" -j "${JOBS}"

echo
echo "==> Build complete. Binaries:"
echo "    ${REPO_ROOT}/${BUILD_DIR}/sample_player"
echo "    ${REPO_ROOT}/${BUILD_DIR}/make_disk_image"
echo
echo "Run it with (from the repo root):"
echo "    ./${BUILD_DIR}/make_disk_image my_wav_files disk.img"
echo "    ./${BUILD_DIR}/sample_player"
