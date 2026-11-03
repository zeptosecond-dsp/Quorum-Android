# SPDX-License-Identifier: GPL-3.0-only
# libquorum: Quorum's engine (engine/firmware, unchanged but for the platform switch) on the Android platform layer
# (engine/android), with Oboe for the sound.
cmake_minimum_required(VERSION 3.22.1)
project(quorum C CXX)

find_package(oboe REQUIRED CONFIG)

set(ENG ${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine)

add_library(quorum SHARED
    quorum_jni.cpp
    ${ENG}/firmware/src/dinghy.c)

target_include_directories(quorum PRIVATE
    ${ENG}/android
    ${ENG}/firmware/hal
    ${ENG}/firmware/src
    ${ENG}/firmware/gen)

target_compile_definitions(quorum PRIVATE QUORUM_ANDROID DINGHY_HOST "APP_VER=\"A0.1\"")

# the firmware is built as its own authors build it; its warnings are theirs
set_source_files_properties(${ENG}/firmware/src/dinghy.c PROPERTIES COMPILE_OPTIONS "-O2;-w;-std=gnu11")

target_link_libraries(quorum oboe::oboe log android)
