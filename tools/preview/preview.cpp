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

    ui::View applet = library();
    applet.applet_mode = true;
    applet.toast = "Dread is running in the background. Close it first (press HOME, then X on Dread), then install. Nothing on the SD card was changed.";
    applet.toast_bad = true;
    applet.show_toast = true;
    shot("9-applet-dread-running", applet);

    ui::View update = library();
    update.update_version = "0.4.3";
    shot("10-update-available", update);
    update.applet_mode = true;
    shot("10b-update-in-applet-mode", update);

    // A long address beside a lit version chip: the address gives way.
    ui::View crowded = library();
    crowded.server = "https://dreadovania-pairing.a-rather-long-home-domain.example.com:58080";
    crowded.update_version = "0.10.12";
    shot("10c-update-long-address", crowded);
    crowded.update_version.clear();
    crowded.update_required = true;
    shot("10d-update-required", crowded);

    ui::View updating = library();
    updating.screen = ui::Screen::Working;
    updating.work.title = "Updating the app to 0.4.3";
    updating.work.steps = {"Download and check", "Replace the app", "Restart"};
    updating.work.fraction = 0.4;
    updating.work.detail = "";
    shot("11-updating", updating);

    ui::View unpaired;
    shot("8-first-run", unpaired);

    // Several servers (ADR 0010).
    ui::View servers = library();
    servers.screen = ui::Screen::Servers;
    servers.network = "Olsen Home 5G";
    servers.servers = {
        {"https://dread.home.example.com", "Nate's OG Switch", {"Olsen Home 5G", "Olsen Home"}, true},
        {"https://dread.friend.example.net:8443", "Nate's OG Switch", {}, false},
    };
    shot("12-servers", servers);
    servers.server_cursor = 1;
    servers.network = "Cafe Guest";
    shot("12b-servers-other-network", servers);
    ui::View dialog = servers;
    dialog.dialog = ui::Dialog::Unpair;
    dialog.unpair_target = "https://dread.friend.example.net:8443";
    shot("12c-unpair-a-server", dialog);

    ui::View not_linked = library();
    not_linked.server_note = "Not linked to Cafe Guest";
    shot("13-library-not-linked", not_linked);

    ui::View checked = not_linked;
    checked.toast = "This app is up to date (0.6.0).";
    checked.show_toast = true;
    shot("15-checked-for-updates", checked);

    ui::View another;
    another.pairing_another = true;
    shot("14-pair-another", another);

    ui::View starting;
    starting.screen = ui::Screen::Starting;
    starting.busy = "Preparing trusted certificates (2 of 4)...";
    starting.busy_frame = 3;
    shot("16-starting", starting);

    ui::View busy = library();
    busy.busy = "Loading seeds from dread.home.example.com...";
    busy.busy_frame = 5;
    shot("17-busy", busy);

    ui::View failed_pairing;
    failed_pairing.last_server = "https://dread.example.com";
    failed_pairing.typed_code = "GW3YW6P6";
    failed_pairing.toast =
        "Not paired: the server's certificate isn't trusted. Your CA from "
        "/config/dreadovania/ca.pem was used. Also check the Switch's date and time (it says "
        "2026-11-12 19:47 UTC): a new certificate looks \"not yet valid\" to a clock that is "
        "behind. Tried: Switch; own CA, RSA. All: not trusted";
    failed_pairing.toast_bad = true;
    failed_pairing.show_toast = true;
    shot("18-pairing-attempts", failed_pairing);

    g.shutdown();
    return failures == 0 ? 0 : 1;
}
