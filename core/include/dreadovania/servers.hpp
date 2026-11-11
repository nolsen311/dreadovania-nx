// SPDX-License-Identifier: GPL-3.0-or-later
// The servers this Switch is paired with (ADR 0010), kept on the SD card.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dreadovania/api.hpp"

namespace dreadovania {

inline constexpr std::string_view kServersFile = "/config/dreadovania/servers.json";

struct PairedServer {
    std::string server;  // an origin, e.g. https://dread.home.example.com
    std::string device_id;
    std::string name;
    std::string token;                  // secret: never shown or logged
    std::vector<std::string> networks;  // network names linked to this server
    std::int64_t last_used = 0;         // a counter: higher is more recent
};

struct ServerList {
    std::vector<PairedServer> servers;
};

// Unusable entries (no address or token) are skipped; nothing if it isn't a list.
std::optional<ServerList> parse_server_list(std::string_view text);
std::string serialize_server_list(const ServerList& list);

// The upgrade from the single-server settings file (device.json).
ServerList server_list_from(const DeviceConfig& config);

// Add a newly paired server, or replace the entry with the same address (its
// linked networks are kept). It becomes the most recently used. Returns its index.
std::size_t remember_server(ServerList& list, PairedServer server);

// Make an entry the most recently used.
void mark_used(ServerList& list, std::size_t index);

// Remove a server by address. Returns whether it was there.
bool forget_server(ServerList& list, std::string_view origin);

// The one used most recently (ADR 0010 rules 2 and 3), or nothing if the list is empty.
std::optional<std::size_t> most_recent(const ServerList& list);

}  // namespace dreadovania
