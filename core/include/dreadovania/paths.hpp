// SPDX-License-Identifier: GPL-3.0-or-later
// SD card paths and the safety rule shared with the server's Send to Switch
// (dreadovania-server push.py; ADR 0004 here): Dreadovania only ever writes or
// deletes inside Dread's two mod folders.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace dreadovania {

inline constexpr std::string_view kDreadTitleId = "010093801237c000";
inline constexpr std::array<std::string_view, 2> kModRoots = {
    "/atmosphere/contents/010093801237c000",
    "/atmosphere/exefs_patches/DreadRandovania",
};
// The record of what Dreadovania installed; the server writes the same file.
inline constexpr std::string_view kSdManifest = "/config/dreadovania/installed.json";

// An absolute path with "." and empty segments removed. Returns nothing for a
// relative path, a path with "..", or one with control characters.
std::optional<std::string> normalize_sd_path(std::string_view path);

// True only for a file strictly inside one of kModRoots.
bool in_mod_roots(std::string_view path);

}  // namespace dreadovania
