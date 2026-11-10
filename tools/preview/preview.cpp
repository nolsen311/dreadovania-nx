// SPDX-License-Identifier: GPL-3.0-or-later
// Draws every screen of the app with sample data and saves each as a PNG, so
// the layout can be checked without a Switch. Usage: preview <output folder>.
// The names, hashes and address are made up.
#include <SDL.h>

#include <cstdio>
#include <string>

#include "ui/gfx.hpp"
#include "ui/screens.hpp"

namespace {

ui::View library() {
    ui::View v;
    v.screen = ui::Screen::Library;
    v.server = "https://dread.home.example.com";
    v.connected = true;
    v.loaded = true;
    v.seeds = {
        {"s1", "ready", "Big tanks", "2026-11-10T01:00:00+00:00", "Kraid Pulse Morph", "ready", "j1", "m1"},
        {"s2", "ready", "Starter Preset", "2026-11-09T20:00:00+00:00", "Grapple Tank Shinespark", "ready", "j2", "m2"},
        {"s3", "ready", "Starter Preset", "2026-11-08T20:00:00+00:00", "Ice Flash Spider", "", "j3", ""},
        {"s4", "ready", "Permalink", "2026-11-08T10:00:00+00:00", "Phantom Cloak Diffusion", "pending", "j4", "m4"},
        {"s5", "failed", "April Fools 2023", "2026-11-07T10:00:00+00:00", "", "", "j5", ""},
        {"s6", "pending", "A preset with a very long name that won't fit on one line at all", "2026-11-06T10:00:00+00:00", "", "", "j6", ""},
    };
    v.installed = "s2";
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: preview <output folder>\n");
        return 2;
    }
    const std::string out = argv[1];
    ui::Gfx g;
    std::string error = g.init();
    if (!error.empty()) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    int failures = 0;
    auto shot = [&](const char* name, const ui::View& v) {
        ui::draw(g, v);
        if (!g.save_png((out + "/" + name + ".png").c_str())) {
            std::fprintf(stderr, "couldn't save %s: %s\n", name, SDL_GetError());
            ++failures;
        }
    };

    shot("1-library", library());

    ui::View presets = library();
    presets.screen = ui::Screen::Presets;
    presets.presets = {
        {"starter_preset", "Starter Preset",
         "This preset is designed for those new to Dread or its randomizer. Every seed can be "
         "completed without utilizing any tricks or glitches.<br/><br/>Along with shuffling "
         "items, some changes have been made to improve the randomizer experience."},
        {"user:1", "Big tanks", ""},
        {"april_fools_2023", "April Fools 2023", "Preset for April Fools Day 2023. Be careful around those red Missile Tanks!"},
    };
    presets.race = true;
    shot("2-new-seed", presets);

    ui::View working = library();
    working.screen = ui::Screen::Working;
    working.work.title = "Getting your new seed";
    working.work.steps = {"Generate", "Patch with your game files", "Download and check", "Install"};
    working.work.current = 2;
    working.work.seed_hash = "Kraid Pulse Morph";
    working.work.detail = "Downloading 14.2 MB of 25.1 MB...";
    working.work.fraction = 0.57;
    shot("3-working", working);

    ui::View pair;
    pair.last_server = "https://dread.home.example.com";
    pair.typed_code = "K7MQR";
    shot("4-pair", pair);

    ui::View confirm = library();
    confirm.dialog = ui::Dialog::Replace;
    confirm.conflicts = {"/atmosphere/contents/010093801237c000/romfs/system/fx/samus.bmsas", "b", "c"};
    shot("5-confirm", confirm);

    ui::View installed = library();
    installed.installed = "s1";
    installed.toast = "Installed (394 files, 25.1 MB). Close this app and start Dread with a new save file.";
    installed.show_toast = true;
    shot("6-installed", installed);

    ui::View failed = library();
    failed.toast = "Not patched: the game files aren't ready: none uploaded (HTTP 409)";
    failed.toast_bad = true;
    failed.show_toast = true;
    shot("7-error", failed);

    ui::View unpaired;
    shot("8-first-run", unpaired);

    g.shutdown();
    return failures == 0 ? 0 : 1;
}
