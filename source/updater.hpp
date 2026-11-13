// SPDX-License-Identifier: GPL-3.0-or-later
// Updating the app in place (server ADR 0021): download the verified .nro from
// the server next to our own, check its size and SHA-256, then swap it in. The
// running app is already in memory, so its file can be replaced; a failure at
// any step leaves a working app in place.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "dreadovania/api.hpp"
// Through the include path, so the host tests can supply their own (tests/fake).
#include <http.hpp>

namespace app {

struct UpdateResult {
    bool ok = false;
    std::string message;
};

// Whether `path` (how the app was started, argv[0]) is a .nro on the SD card
// that can be replaced; anything else means "update by hand".
bool updatable_path(const std::string& path);

UpdateResult download_update(Http& http, const std::string& server, const std::string& token,
                             const dreadovania::AppLatest& latest, const std::string& self_path,
                             const std::function<void(double)>& progress,
                             const std::function<void()>& on_replace = {});

}  // namespace app
