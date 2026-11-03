#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the engine with the Android platform layer on this computer and run engine/tests/desktop_check.c.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/desktop
CC="${CC:-clang}"
F=engine/firmware
$CC -O2 -w -DQUORUM_ANDROID -DDINGHY_HOST -DAPP_VER="\"android-dev\"" \
    -Iengine/android -I$F/hal -I$F/src -I$F/gen -c engine/android/quorum_engine.c -o build/desktop/engine.o
$CC -O2 -Wall -c engine/tests/desktop_check.c -o build/desktop/check.o
$CC build/desktop/engine.o build/desktop/check.o -lpthread -lm -o build/desktop/check
build/desktop/check build/desktop
$CC -O2 -w -c engine/tests/touch_check.c -o build/desktop/touch.o
$CC build/desktop/engine.o build/desktop/touch.o -lpthread -lm -o build/desktop/touch
build/desktop/touch && echo "touch: screens in build/desktop/touch-*.ppm"
