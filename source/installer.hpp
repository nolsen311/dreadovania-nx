// SPDX-License-Identifier: GPL-3.0-or-later
// Downloads a patched seed and installs it (server ADR 0018): stage, check every
// file, then install with the SD-card record written first, as Send to Switch does.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Through the include path, so the host tests can supply their own (tests/fake).
#include <http.hpp>

namespace app {

struct InstallResult {
    bool installed = false;
    std::string message;
    std::vector<std::string> conflicts;  // other mods' files; install again with overwrite
};

InstallResult install_seed(Http& http, const std::string& server, const std::string& token,
                           const std::string& seed_id, bool overwrite,
                           const std::function<void(const std::string&)>& status,
                           const std::function<void(double)>& progress = {});  // 0..1, downloading

// The seed the SD card says is installed, if any.
std::optional<std::string> installed_seed();

}  // namespace app
