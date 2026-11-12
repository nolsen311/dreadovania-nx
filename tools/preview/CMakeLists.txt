# SPDX-License-Identifier: GPL-3.0-or-later
# Draws the app's screens on a computer with sample data and saves them as PNGs
# (ADR 0009), with the same drawing code as the Switch. Needs SDL2, SDL2_ttf,
# SDL2_gfx and SDL2_image, and a font file in DREADOVANIA_FONT.
cmake_minimum_required(VERSION 3.20)
project(dreadovania_preview CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl2 SDL2_ttf SDL2_gfx SDL2_image)
add_executable(preview preview.cpp ../../source/ui/gfx.cpp ../../source/ui/widgets.cpp
               ../../source/ui/screens.cpp ../../core/src/api.cpp ../../core/src/json.cpp
               ../../core/src/certs.cpp)
target_include_directories(preview PRIVATE ../../core/include ../../source)
target_compile_definitions(preview PRIVATE DREADOVANIA_VERSION="preview")
target_compile_options(preview PRIVATE -Wall -Wextra -Werror)
target_link_libraries(preview PRIVATE PkgConfig::SDL)
