// SPDX-License-Identifier: GPL-3.0-or-later
// The app's screens (docs/design, ADR 0009), drawn from a plain description of
// what to show. The app owns a View and changes it; draw() turns it into pixels.
// Nothing here talks to the network or the SD card, so tools/preview can draw
// the same screens on a computer.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "dreadovania/api.hpp"
#include "ui/gfx.hpp"

namespace ui {

enum class Screen { Pair, Library, Presets, Working };
enum class Dialog { None, Replace, Unpair };

struct Work {
    std::string title;
    std::vector<std::string> steps;
    std::size_t current = 0;
    std::string detail;  // the server's progress line, or the download size
    std::string seed_hash;
    double fraction = -1;  // the download's progress, or -1
};

struct View {
    Screen screen = Screen::Pair;
    Dialog dialog = Dialog::None;
    std::string server;  // shown at the top right; "" when not paired
    bool connected = false;

    std::vector<dreadovania::SeedSummary> seeds;
    std::size_t cursor = 0;
    bool loaded = false;
    std::string installed;  // the seed the SD card says is installed
    std::vector<std::string> conflicts;

    std::vector<dreadovania::PresetSummary> presets;
    std::size_t preset_cursor = 0;
    bool race = false;

    Work work;

    std::string last_server;
    std::string typed_code;
    dreadovania::UserCa user_ca = dreadovania::UserCa::Missing;
    bool applet_mode = false;  // opened from the Album: less memory, a game may be suspended
    std::string update_version;  // a newer verified app from the server, or ""
    bool update_required = false;  // this app is older than the server works with

    std::string toast;
    bool toast_bad = false;
    bool show_toast = false;
};

// Layout shared with touch input.
inline constexpr int kRowH = 80;
inline constexpr int kRowGap = 8;
inline constexpr int kAsideW = 360;
std::size_t visible_rows();
std::size_t first_visible(std::size_t cursor, std::size_t count);
int list_top();

void draw(Gfx& g, const View& v);

}  // namespace ui
