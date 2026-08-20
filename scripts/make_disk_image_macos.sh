#!/usr/bin/env bash
#
# make_disk_image_macos.sh - Packs a folder's contents into a FAT disk image sized to fit it,
# using the make_disk_image CLI tool built from platform/linux/tools/make_disk_image.cpp (see
# README.md's "Option A - Native build" section).
#
# Usage:
#   scripts/make_disk_image_macos.sh <source_folder> [output_image.img] [build_dir]
#
#   source_folder   Folder whose contents get packed into the image (required).
#   output_image    Path to the disk image to create. Defaults to "disk.img" in the repo root.
#   build_dir       Build directory make_disk_image lives in (or gets built into if missing).
#                   Defaults to "build-macos" (matching scripts/build_macos.sh).
#
# The image is sized from the folder's actual on-disk usage (via `du`) plus a safety margin, so
# it comfortably fits the FAT filesystem's own metadata (boot sector, FAT tables, root
# directory) on top of the file data itself, instead of relying on make_disk_image's fixed
# 32 MiB default (see its --help/usage banner). If make_disk_image isn't built yet, this script
# builds it via scripts/build_macos.sh first.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: this script is for macOS only (detected: $(uname -s))" >&2
    exit 1
fi

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <source_folder> [output_image.img] [build_dir]" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SOURCE_FOLDER="$1"
OUTPUT_IMAGE="${2:-${REPO_ROOT}/disk.img}"
BUILD_DIR="${3:-${REPO_ROOT}/build-macos}"

if [[ ! -d "${SOURCE_FOLDER}" ]]; then
    echo "error: source folder does not exist or is not a directory: ${SOURCE_FOLDER}" >&2
    exit 1
fi

MAKE_DISK_IMAGE_BIN="${BUILD_DIR}/make_disk_image"
if [[ ! -x "${MAKE_DISK_IMAGE_BIN}" ]]; then
    echo "==> make_disk_image not found at ${MAKE_DISK_IMAGE_BIN}; building it via build_macos.sh..."
    "${SCRIPT_DIR}/build_macos.sh" "$(basename "${BUILD_DIR}")"
fi

if [[ ! -x "${MAKE_DISK_IMAGE_BIN}" ]]; then
    echo "error: make_disk_image still not found/executable after building: ${MAKE_DISK_IMAGE_BIN}" >&2
    exit 1
fi

# ---- Size the image to fit SOURCE_FOLDER ----
#
# `du -sk` reports the folder's actual on-disk usage in 1 KiB blocks (portable across the BSD
# `du` shipped on macOS and GNU `du`). Bytes are then padded with a 25% safety margin (FAT
# metadata: boot sector, FAT tables, root directory entries all take extra space on top of raw
# file bytes, and short 8.3 names round file sizes up to whole clusters -- see FatFsCore/
# ff15/source/ffconf.h's FF_USE_LFN == 0 note in platform/linux/tools/make_disk_image.cpp),
# then converted to whole 512-byte sectors and rounded up to a 1 MiB boundary for a tidy image
# size. A 4 MiB floor keeps tiny folders from producing a volume too small for FAT to format.
FOLDER_KIB="$(du -sk "${SOURCE_FOLDER}" | awk '{print $1}')"
FOLDER_BYTES=$((FOLDER_KIB * 1024))
PADDED_BYTES=$((FOLDER_BYTES * 5 / 4))

SECTOR_SIZE=512
MIB_SECTORS=$((1024 * 1024 / SECTOR_SIZE))   # sectors per 1 MiB
MIN_SECTORS=$((4 * MIB_SECTORS))             # 4 MiB floor

SECTOR_COUNT=$(((PADDED_BYTES + SECTOR_SIZE - 1) / SECTOR_SIZE))
# Round up to the next whole MiB.
SECTOR_COUNT=$((((SECTOR_COUNT + MIB_SECTORS - 1) / MIB_SECTORS) * MIB_SECTORS))
if [[ "${SECTOR_COUNT}" -lt "${MIN_SECTORS}" ]]; then
    SECTOR_COUNT=${MIN_SECTORS}
fi

IMAGE_MIB=$((SECTOR_COUNT * SECTOR_SIZE / 1024 / 1024))
echo "==> ${SOURCE_FOLDER} uses ${FOLDER_KIB} KiB; sizing image to ${IMAGE_MIB} MiB (${SECTOR_COUNT} sectors)"

echo "==> Packing ${SOURCE_FOLDER} into ${OUTPUT_IMAGE}..."
"${MAKE_DISK_IMAGE_BIN}" "${SOURCE_FOLDER}" "${OUTPUT_IMAGE}" "${SECTOR_COUNT}"

echo "==> Done: ${OUTPUT_IMAGE}"
