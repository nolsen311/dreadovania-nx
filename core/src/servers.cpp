// SPDX-License-Identifier: GPL-3.0-or-later
#include "dreadovania/servers.hpp"

#include <algorithm>

#include "dreadovania/json.hpp"

namespace dreadovania {

namespace {

std::int64_t next_use(const ServerList& list) {
    std::int64_t highest = 0;
    for (const PairedServer& s : list.servers) {
        highest = std::max(highest, s.last_used);
    }
    return highest + 1;
}

}  // namespace

std::optional<ServerList> parse_server_list(std::string_view text) {
    json::ParseResult parsed = json::parse(text);
    if (!parsed.value || !parsed.value->is_object()) {
        return std::nullopt;
    }
    const json::Value* servers = parsed.value->get("servers");
    if (servers == nullptr || !servers->is_array()) {
        return std::nullopt;
    }
    ServerList list;
    for (const json::Value& item : servers->items()) {
        if (!item.is_object()) {
            continue;
        }
        auto address = item.string_at("server");
        auto token = item.string_at("token");
        if (!address || !token || token->empty()) {
            continue;
        }
        ServerUrl url = normalize_server_url(*address);
        if (!url.origin) {
            continue;
        }
        bool duplicate = std::any_of(list.servers.begin(), list.servers.end(),
                                     [&](const PairedServer& s) { return s.server == *url.origin; });
        if (duplicate) {
            continue;
        }
        PairedServer s;
        s.server = *url.origin;
        s.token = *token;
        s.device_id = item.string_at("device_id").value_or("");
        s.name = item.string_at("name").value_or("");
        s.last_used = std::max<std::int64_t>(0, item.int_at("last_used").value_or(0));
        if (const json::Value* networks = item.get("networks"); networks && networks->is_array()) {
            for (const json::Value& n : networks->items()) {
                if (n.is_string() && !n.as_string().empty()) {
                    s.networks.push_back(n.as_string());
                }
            }
        }
        list.servers.push_back(std::move(s));
    }
    return list;
}

std::string serialize_server_list(const ServerList& list) {
    std::string out = "{\n  \"servers\": [";
    for (std::size_t i = 0; i < list.servers.size(); ++i) {
        const PairedServer& s = list.servers[i];
        out += i == 0 ? "\n" : ",\n";
        out += "    {\n      \"server\": " + json::quote(s.server) +
               ",\n      \"device_id\": " + json::quote(s.device_id) +
               ",\n      \"name\": " + json::quote(s.name) +
               ",\n      \"token\": " + json::quote(s.token) + ",\n      \"networks\": [";
        for (std::size_t n = 0; n < s.networks.size(); ++n) {
            out += (n == 0 ? "" : ", ") + json::quote(s.networks[n]);
        }
        out += "],\n      \"last_used\": " + std::to_string(s.last_used) + "\n    }";
    }
    out += list.servers.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

ServerList server_list_from(const DeviceConfig& config) {
    ServerList list;
    list.servers.push_back({config.server, config.device_id, config.name, config.token, {}, 1});
    return list;
}

std::size_t remember_server(ServerList& list, PairedServer server) {
    server.last_used = next_use(list);
    for (std::size_t i = 0; i < list.servers.size(); ++i) {
        if (list.servers[i].server == server.server) {
            server.networks = std::move(list.servers[i].networks);
            list.servers[i] = std::move(server);
            return i;
        }
    }
    list.servers.push_back(std::move(server));
    return list.servers.size() - 1;
}

void mark_used(ServerList& list, std::size_t index) {
    if (index < list.servers.size()) {
        list.servers[index].last_used = next_use(list);
    }
}

bool forget_server(ServerList& list, std::string_view origin) {
    auto it = std::find_if(list.servers.begin(), list.servers.end(),
                           [&](const PairedServer& s) { return s.server == origin; });
    if (it == list.servers.end()) {
        return false;
    }
    list.servers.erase(it);
    return true;
}

std::optional<std::size_t> most_recent(const ServerList& list) {
    if (list.servers.empty()) {
        return std::nullopt;
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < list.servers.size(); ++i) {
        if (list.servers[i].last_used > list.servers[best].last_used) {
            best = i;
        }
    }
    return best;
}

std::optional<std::size_t> linked_server(const ServerList& list, std::string_view network) {
    if (network.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < list.servers.size(); ++i) {
        const auto& networks = list.servers[i].networks;
        if (std::find(networks.begin(), networks.end(), network) != networks.end()) {
            return i;
        }
    }
    return std::nullopt;
}

bool link_network(ServerList& list, std::size_t index, std::string_view network) {
    if (network.empty() || index >= list.servers.size()) {
        return false;
    }
    for (std::size_t i = 0; i < list.servers.size(); ++i) {
        if (i != index) {
            unlink_network(list, i, network);
        }
    }
    auto& networks = list.servers[index].networks;
    if (std::find(networks.begin(), networks.end(), network) == networks.end()) {
        networks.emplace_back(network);
    }
    return true;
}

bool unlink_network(ServerList& list, std::size_t index, std::string_view network) {
    if (index >= list.servers.size()) {
        return false;
    }
    auto& networks = list.servers[index].networks;
    auto it = std::remove(networks.begin(), networks.end(), network);
    bool found = it != networks.end();
    networks.erase(it, networks.end());
    return found;
}

ServerChoice choose_server(const ServerList& list, std::string_view network) {
    if (auto linked = linked_server(list, network)) {
        return {linked, ChoiceReason::Network};
    }
    if (list.servers.size() == 1) {
        return {std::size_t{0}, ChoiceReason::OnlyServer};
    }
    if (auto recent = most_recent(list)) {
        return {recent, ChoiceReason::LastUsed};
    }
    return {};
}

}  // namespace dreadovania
