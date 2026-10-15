#!/usr/bin/env bash
# Builds librealsense for ofxLibRealSense2 on macOS: Release, static, universal (arm64 + x86_64),
# macOS 11.0+, libusb (RSUSB) backend, NEON on arm64 / SSSE3 + AVX on x86_64. Installs the
# headers and one libs/librealsense2/lib/osx/librealsense2.a.
#
# librealsense's C++ API (rs.hpp) is header-only on top of its C API, so the library and
# everything it bundles (libusb, json, easylogging, lz4, rosbag...) are pre-linked into one
# object that exports only rs2_*: nothing inside can clash with openFrameworks or other addons
# (e.g. ofxKinectV2's libusb).
#
# Requirements: full Xcode, Homebrew cmake. Each architecture is its own build, joined with lipo:
# librealsense picks its SIMD code from `$CC -dumpmachine`, so the cross-built half uses
# compiler wrappers that target (and report) that architecture.
#
# Usage: ./build_librealsense_macos.sh /path/to/librealsense [--native]
#   --native   this Mac's architecture only (faster; not for committing)

set -euo pipefail

SOURCE="$(cd "${1:?usage: $0 /path/to/librealsense [--native]}" && pwd)"
shift
ADDON="$(cd "$(dirname "$0")/.." && pwd)"
ARCHS=(arm64 x86_64)
for arg in "$@"; do
    case "$arg" in
        --native) ARCHS=("$(uname -m)") ;;
        *) echo "unknown option: $arg" >&2; exit 1 ;;
    esac
done
export MACOSX_DEPLOYMENT_TARGET=11.0 # also reaches the libusb sub-build
WORK="$SOURCE/build_ofx"
VERSION="$(awk '/#define RS2_API_(MAJOR|MINOR|PATCH)_VERSION/ {printf "%s%s", sep, $3; sep="."}' "$SOURCE/include/librealsense2/rs.h")"

# The SDK hands its toolchain file on to the libusb sub-build, so each architecture gets one
# that sets the architecture (and, cross-building, the compiler wrappers), stops libusb calling
# pipe2() (declared by the SDK but only macOS 27+ has it: unguarded it would crash older
# systems) and compiles without common symbols, so libusb's globals become ordinary
# definitions that the export list below can hide.
SLICES=()
for a in "${ARCHS[@]}"; do
    B="$WORK/$a"
    mkdir -p "$B"
    TOOLCHAIN="$B/ofx-toolchain.cmake"
    {
        echo "set(CMAKE_OSX_ARCHITECTURES $a CACHE STRING \"\")"
        echo "set(CMAKE_OSX_DEPLOYMENT_TARGET $MACOSX_DEPLOYMENT_TARGET CACHE STRING \"\")"
        echo 'set(HAVE_PIPE2 0 CACHE INTERNAL "pipe2 is macOS 27+ only")'
        echo 'set(CMAKE_C_FLAGS_INIT "-fno-common")'
    } > "$TOOLCHAIN"
    if [ "$a" != "$(uname -m)" ]; then
        for tool in clang clang++; do
            printf '#!/bin/sh\nexec xcrun %s -target %s-apple-macos%s "$@"\n' "$tool" "$a" "$MACOSX_DEPLOYMENT_TARGET" > "$B/$tool-$a"
            chmod +x "$B/$tool-$a"
        done
        echo "set(CMAKE_C_COMPILER \"$B/clang-$a\")" >> "$TOOLCHAIN"
        echo "set(CMAKE_CXX_COMPILER \"$B/clang++-$a\")" >> "$TOOLCHAIN"
    fi
    cmake -S "$SOURCE" -B "$B" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_RS2_ALL=OFF \
        -DFORCE_RSUSB_BACKEND=ON \
        -DUSE_EXTERNAL_USB=ON \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_GRAPHICAL_EXAMPLES=OFF \
        -DBUILD_GLSL_EXTENSIONS=OFF \
        -DBUILD_TOOLS=OFF \
        -DBUILD_UNIT_TESTS=OFF \
        -DBUILD_WITH_OPENMP=OFF \
        -DCHECK_FOR_UPDATES=OFF \
        -DENABLE_STATS=OFF \
        -DENABLE_CCACHE=OFF \
        -DBUILD_WITH_DDS=OFF \
        -DBUILD_ROSBAG2=OFF
    cmake --build "$B" --parallel --target realsense2

    # Pre-link librealsense, its static dependencies and libusb into one object exporting only
    # the C API. (librealsense's own realsense2-all bundle needs GNU ar; this replaces it.)
    P="$B/prelink"
    rm -rf "$P" && mkdir -p "$P"
    echo '_rs2_*' > "$P/exports.txt"
    LOAD=()
    for lib in $(find "$B/Release" -name '*.a') "$B/libusb_install/lib/libusb-1.0.a"; do
        # rsutils compiles its own copy of easylogging++, as librealsense2 does: keep one
        if [ "$(basename "$lib")" = librsutils.a ]; then
            cp "$lib" "$P/librsutils.a" && ar -d "$P/librsutils.a" easylogging++.cc.o && lib="$P/librsutils.a"
        fi
        LOAD+=(-force_load "$lib")
    done
    ld -r -arch "$a" -platform_version macos "$MACOSX_DEPLOYMENT_TARGET" "$(xcrun --show-sdk-version)" \
        "${LOAD[@]}" -exported_symbols_list "$P/exports.txt" -o "$P/realsense2.o"
    libtool -static -o "$P/librealsense2.a" "$P/realsense2.o"
    SLICES+=("$P/librealsense2.a")
done

INC_OUT="$ADDON/libs/librealsense2/include"
LIB_OUT="$ADDON/libs/librealsense2/lib/osx"
rm -rf "$INC_OUT" "$LIB_OUT"
mkdir -p "$INC_OUT" "$LIB_OUT"
cp -R "$SOURCE/include/librealsense2" "$INC_OUT/"
lipo -create "${SLICES[@]}" -output "$LIB_OUT/librealsense2.a"

echo
echo "librealsense $VERSION ($(git -C "$SOURCE" describe --tags --always 2>/dev/null)) installed into $ADDON/libs/librealsense2"
echo "  librealsense2.a: $(lipo -archs "$LIB_OUT/librealsense2.a"), macOS $MACOSX_DEPLOYMENT_TARGET+, $(du -h "$LIB_OUT/librealsense2.a" | cut -f1)"
