// SPDX-License-Identifier: GPL-3.0-or-later
// What the console is doing around the app: how it was opened, and which game,
// if any, is running alongside it (for dreadovania::install_blocker).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace app {

struct SystemState {
    bool app_is_application = true;              // false: opened as an applet (Album)
    std::optional<std::uint64_t> running_program;  // the game running alongside, if any
};

// Asks the system now; a suspended game can be closed while the app is open.
SystemState system_state();

// The name of the network the console is connected to (ADR 0010): the Wi-Fi's
// SSID, or the network profile's name on a wired connection. "" when it isn't
// connected or the system won't say. Only read; never sent anywhere.
std::string current_network();

}  // namespace app
