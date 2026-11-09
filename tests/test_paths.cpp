// SPDX-License-Identifier: GPL-3.0-or-later
// The path safety rule matches the server's (dreadovania-server push.py,
// in_mod_roots): only files strictly inside Dread's two mod folders.
#include "check.hpp"
#include "dreadovania/paths.hpp"

using dreadovania::in_mod_roots;
using dreadovania::normalize_sd_path;

TEST(files_inside_the_mod_folders_are_allowed) {
    CHECK(in_mod_roots("/atmosphere/contents/010093801237c000/romfs/system.pkg"));
    CHECK(in_mod_roots("/atmosphere/contents/010093801237c000/exefs/main.npdm"));
    CHECK(in_mod_roots("/atmosphere/exefs_patches/DreadRandovania/abc.ips"));
    CHECK(in_mod_roots("/atmosphere//contents/./010093801237c000/romfs/a"));
}

TEST(the_folders_themselves_and_everything_else_are_refused) {
    CHECK(!in_mod_roots("/atmosphere/contents/010093801237c000"));
    CHECK(!in_mod_roots("/atmosphere/contents/010093801237c000/"));
    CHECK(!in_mod_roots("/atmosphere/contents/0100000000001000/romfs/a"));
    CHECK(!in_mod_roots("/atmosphere/contents/010093801237c000x/romfs/a"));
    CHECK(!in_mod_roots("/atmosphere/config/system_settings.ini"));
    CHECK(!in_mod_roots("/config/dreadovania/installed.json"));
    CHECK(!in_mod_roots("/"));
    CHECK(!in_mod_roots(""));
}

TEST(crafted_paths_are_refused) {
    // A manifest on the SD card can be edited by anyone.
    CHECK(!in_mod_roots("/atmosphere/contents/010093801237c000/../../config/x"));
    CHECK(!in_mod_roots("/atmosphere/contents/010093801237c000/romfs/.."));
    CHECK(!in_mod_roots("atmosphere/contents/010093801237c000/romfs/a"));
    CHECK(!in_mod_roots("/atmosphere/contents/010093801237c000/romfs/a\nb"));
    CHECK(!in_mod_roots("/atmosphere\\contents\\010093801237c000\\romfs\\a"));
    CHECK(!in_mod_roots(std::string_view("/atmosphere/contents/010093801237c000/a\0b", 42)));
}

TEST(normalizing) {
    CHECK(normalize_sd_path("/a//b/./c/") == "/a/b/c");
    CHECK(normalize_sd_path("/") == "/");
    CHECK(!normalize_sd_path("relative/path"));
    CHECK(!normalize_sd_path("/a/../b"));
}

int main() { return check::run_all(); }
