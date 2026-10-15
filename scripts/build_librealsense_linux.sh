#!/usr/bin/env bash
# Builds librealsense for ofxLibRealSense2 on Linux (x86_64 or aarch64): Release, static,
# libusb (RSUSB) backend - no kernel patches, just the udev rules. Installs the headers and
# one libs/librealsense2/lib/linux64/librealsense2.a (linuxaarch64 on ARM).
#
# As on macOS, librealsense and everything it bundles (libusb, json, easylogging, lz4...) are
# pre-linked into one object that exports only the C API (rs2_*), so nothing inside can clash
# with openFrameworks or other addons.
#
# Keep the CMake options in sync with build_librealsense_macos.sh / _windows.ps1.
#
# Requirements (Ubuntu 22.04+):
#   sudo apt install build-essential cmake git pkg-config libudev-dev libssl-dev
# Then let users open the cameras without root:
#   sudo cp /path/to/librealsense/config/99-realsense-libusb.rules /etc/udev/rules.d/
#   sudo udevadm control --reload-rules && sudo udevadm trigger
#
# Usage: ./build_librealsense_linux.sh /path/to/librealsense [--v4l2]
#   --v4l2   kernel (V4L2) backend instead of libusb: needs librealsense's kernel patches for
#            frame metadata, but is what Intel's own Debian packages use

set -euo pipefail

SOURCE="$(cd "${1:?usage: $0 /path/to/librealsense [--v4l2]}" && pwd)"
shift
ADDON="$(cd "$(dirname "$0")/.." && pwd)"
RSUSB=ON
for arg in "$@"; do
    case "$arg" in
        --v4l2) RSUSB=OFF ;;
        *) echo "unknown option: $arg" >&2; exit 1 ;;
    esac
done
case "$(uname -m)" in
    aarch64|arm64) PLATFORM=linuxaarch64 ;;
    *) PLATFORM=linux64 ;;
esac
B="$SOURCE/build_ofx"

cmake -S "$SOURCE" -B "$B" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_RS2_ALL=OFF \
    -DFORCE_RSUSB_BACKEND="$RSUSB" \
    -DUSE_EXTERNAL_USB=ON \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_GRAPHICAL_EXAMPLES=OFF \
    -DBUILD_GLSL_EXTENSIONS=OFF \
    -DBUILD_TOOLS=OFF \
    -DBUILD_UNIT_TESTS=OFF \
    -DBUILD_WITH_OPENMP=OFF \
    -DBUILD_WITH_CUDA=OFF \
    -DCHECK_FOR_UPDATES=OFF \
    -DENABLE_STATS=OFF \
    -DENABLE_CCACHE=OFF \
    -DBUILD_WITH_DDS=OFF \
    -DBUILD_ROSBAG2=OFF
cmake --build "$B" --parallel --target realsense2

# Pre-link everything into one object, then make every global symbol but rs2_* local.
P="$B/prelink"
rm -rf "$P" && mkdir -p "$P"
LIBS=$(find "$B/Release" "$B" -maxdepth 1 -name '*.a' 2>/dev/null | sort -u)
USB="$B/libusb_install/lib/libusb-1.0.a"
[ -f "$USB" ] && LIBS="$LIBS $USB"
# rsutils compiles its own copy of easylogging++, as librealsense2 does: keep one
for lib in $LIBS; do
    if [ "$(basename "$lib")" = librsutils.a ]; then
        cp "$lib" "$P/librsutils.a" && ar d "$P/librsutils.a" easylogging++.cc.o
        LIBS="${LIBS/$lib/$P/librsutils.a}"
    fi
done
# -d: give common symbols (libusb globals) real storage, so objcopy can make them local
# shellcheck disable=SC2086
ld -r -d -o "$P/realsense2.o" --whole-archive $LIBS --no-whole-archive
objcopy --wildcard --keep-global-symbol='rs2_*' "$P/realsense2.o" "$P/realsense2-ofx.o"
rm -f "$P/librealsense2.a"
ar rcs "$P/librealsense2.a" "$P/realsense2-ofx.o"

INC_OUT="$ADDON/libs/librealsense2/include"
LIB_OUT="$ADDON/libs/librealsense2/lib/$PLATFORM"
rm -rf "$INC_OUT" "$LIB_OUT"
mkdir -p "$INC_OUT" "$LIB_OUT"
cp -R "$SOURCE/include/librealsense2" "$INC_OUT/"
cp "$P/librealsense2.a" "$LIB_OUT/"

echo
echo "librealsense $(git -C "$SOURCE" describe --tags --always 2>/dev/null) installed into $ADDON/libs/librealsense2 ($PLATFORM, $( [ "$RSUSB" = ON ] && echo libusb || echo V4L2 ) backend)"
echo "  exported: $(nm -g --defined-only "$LIB_OUT/librealsense2.a" | grep -c ' rs2_') rs2_* functions"
