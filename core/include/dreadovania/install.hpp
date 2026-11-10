// SPDX-License-Identifier: GPL-3.0-or-later
// Installing a patched seed, the same way as the server's Send to Switch
// (server ADRs 0016 and 0018): download into staging, check every file against
// the server's manifest, then install with the SD-card record written first.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "dreadovania/sha256.hpp"

namespace dreadovania {

struct ModFile {
    std::string sd_path;  // absolute, e.g. /atmosphere/contents/010093801237c000/romfs/x
    std::uint64_t size = 0;
    std::string sha256;
};

struct ModManifest {
    std::vector<ModFile> files;
    std::uint64_t size_bytes = 0;
    std::uint64_t tar_bytes = 0;
};

// The server's /mod/manifest. Every path must be inside Dread's mod folders.
std::optional<ModManifest> parse_mod_manifest(std::string_view body, std::string& error);

// Checks a ustar stream against the manifest as it arrives: same files, same
// order, same sizes and SHA-256s, nothing extra. The callbacks write the files
// out; any of them returning false stops the download.
class TarVerifier {
  public:
    explicit TarVerifier(const ModManifest& manifest) : manifest_(manifest) {}

    std::function<bool(const ModFile&)> on_file_start;
    std::function<bool(const char*, std::size_t)> on_data;
    std::function<bool(const ModFile&)> on_file_end;

    bool feed(const char* data, std::size_t size);
    bool complete() const { return state_ == State::Done; }
    const std::string& error() const { return error_; }

  private:
    enum class State { Header, Data, Padding, End, Done, Failed };
    bool fail(std::string message);
    bool header(const char* block);

    const ModManifest& manifest_;
    State state_ = State::Header;
    std::string error_;
    std::string block_;
    std::size_t index_ = 0;
    std::uint64_t remaining_ = 0;
    std::uint64_t padding_ = 0;
    int zero_blocks_ = 0;
    Sha256 hash_;
};

// The SD card's record of what Dreadovania installed (the server writes the same).
std::vector<std::string> parse_sd_manifest(std::string_view text);  // only paths in the mod folders
std::optional<std::string> sd_manifest_seed(std::string_view text);  // seed ID, if installed
std::string sd_manifest_json(const std::vector<std::string>& files, std::string_view seed_id,
                             std::string_view state);

struct InstallPlan {
    std::vector<std::string> files;      // to install, from the manifest
    std::vector<std::string> remove;     // the previous seed's files that this one doesn't replace
    std::vector<std::string> conflicts;  // files on the card that another mod installed
    std::vector<std::string> record;     // the record to write before changing anything
};

InstallPlan plan_install(const ModManifest& manifest, const std::vector<std::string>& owned,
                         const std::function<bool(const std::string&)>& exists);

// Folders left behind by removed files, deepest first, never the shared ones
// (/atmosphere, /atmosphere/contents, /atmosphere/exefs_patches).
std::vector<std::string> folders_to_tidy(const std::vector<std::string>& removed);

}  // namespace dreadovania
