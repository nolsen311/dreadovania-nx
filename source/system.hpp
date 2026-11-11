// SPDX-License-Identifier: GPL-3.0-or-later
// What the console is doing around the app: how it was opened, and which game,
// if any, is running alongside it (for dreadovania::install_blocker).
#pragma once

#include <cstdint>
#include <optional>

namespace app {

struct SystemState {
    bool app_is_application = true;              // false: opened as an applet (Album)
    std::optional<std::uint64_t> running_program;  // the game running alongside, if any
};

// Asks the system now; a suspended game can be closed while the app is open.
SystemState system_state();

}  // namespace app
