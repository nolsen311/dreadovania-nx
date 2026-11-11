// SPDX-License-Identifier: GPL-3.0-or-later
// The list of paired servers (ADR 0010).
#include <string>

#include "check.hpp"
#include "dreadovania/servers.hpp"

using namespace dreadovania;

namespace {

PairedServer server(const char* origin, const char* token = "dvt_x") {
    PairedServer s;
    s.server = origin;
    s.device_id = "dev";
    s.name = "Switch";
    s.token = token;
    return s;
}

}  // namespace

TEST(server_list_round_trips) {
    ServerList list;
    PairedServer home = server("https://dread.home", "dvt_home");
    home.networks = {"Home Wi-Fi", "Home \"5G\""};
    home.last_used = 3;
    list.servers.push_back(home);
    list.servers.push_back(server("https://dread.friend.lan:8443", "dvt_friend"));
    auto back = parse_server_list(serialize_server_list(list));
    CHECK(back.has_value());
    CHECK(back->servers.size() == 2);
    CHECK(back->servers[0].server == "https://dread.home");
    CHECK(back->servers[0].token == "dvt_home");
    CHECK(back->servers[0].networks.size() == 2);
    CHECK(back->servers[0].networks[1] == "Home \"5G\"");
    CHECK(back->servers[0].last_used == 3);
    CHECK(back->servers[1].server == "https://dread.friend.lan:8443");
    CHECK(back->servers[1].networks.empty());

    auto empty = parse_server_list(serialize_server_list(ServerList{}));
    CHECK(empty.has_value() && empty->servers.empty());
}

TEST(server_list_skips_unusable_entries) {
    auto list = parse_server_list(R"({"servers": [
        {"server": "http://plain.lan", "token": "dvt_a"},
        {"server": "https://no-token.lan"},
        {"server": "https://empty-token.lan", "token": ""},
        "not an object",
        {"server": "HTTPS://Good.lan/", "token": "dvt_b", "networks": ["Home", 5, ""],
         "last_used": -4},
        {"server": "https://good.lan", "token": "dvt_duplicate"}
    ]})");
    CHECK(list.has_value());
    CHECK(list->servers.size() == 1);
    CHECK(list->servers[0].server == "https://good.lan");
    CHECK(list->servers[0].token == "dvt_b");
    CHECK(list->servers[0].networks.size() == 1);
    CHECK(list->servers[0].last_used == 0);
    CHECK(!parse_server_list("[]"));
    CHECK(!parse_server_list("{}"));
    CHECK(!parse_server_list("not json"));
}

TEST(upgrading_from_one_server) {
    DeviceConfig old{"https://dread.home", "dev-1", "Nate's Switch", "dvt_secret"};
    ServerList list = server_list_from(old);
    CHECK(list.servers.size() == 1);
    CHECK(list.servers[0].server == "https://dread.home");
    CHECK(list.servers[0].token == "dvt_secret");
    CHECK(list.servers[0].device_id == "dev-1");
    CHECK(most_recent(list) == std::size_t{0});
}

TEST(remembering_and_forgetting_servers) {
    ServerList list;
    CHECK(!most_recent(list));
    CHECK(remember_server(list, server("https://a.lan", "dvt_1")) == 0);
    CHECK(remember_server(list, server("https://b.lan")) == 1);
    CHECK(most_recent(list) == std::size_t{1});

    list.servers[0].networks = {"Home"};
    CHECK(remember_server(list, server("https://a.lan", "dvt_2")) == 0);  // paired again
    CHECK(list.servers.size() == 2);
    CHECK(list.servers[0].token == "dvt_2");
    CHECK(list.servers[0].networks.size() == 1);  // its networks are kept
    CHECK(most_recent(list) == std::size_t{0});

    mark_used(list, 1);
    CHECK(most_recent(list) == std::size_t{1});
    mark_used(list, 99);  // ignored

    CHECK(forget_server(list, "https://b.lan"));
    CHECK(!forget_server(list, "https://b.lan"));
    CHECK(list.servers.size() == 1);
    CHECK(most_recent(list) == std::size_t{0});
}

TEST(linking_networks) {
    ServerList list;
    remember_server(list, server("https://home.lan"));
    remember_server(list, server("https://friend.lan"));
    CHECK(!linked_server(list, "Home"));
    CHECK(link_network(list, 0, "Home"));
    CHECK(link_network(list, 0, "Home"));  // twice: still once
    CHECK(list.servers[0].networks.size() == 1);
    CHECK(linked_server(list, "Home") == std::size_t{0});
    CHECK(!linked_server(list, "home"));  // SSIDs are case-sensitive

    CHECK(link_network(list, 1, "Home"));  // a network belongs to one server: it moves
    CHECK(linked_server(list, "Home") == std::size_t{1});
    CHECK(list.servers[0].networks.empty());

    CHECK(!link_network(list, 0, ""));
    CHECK(!link_network(list, 5, "Cafe"));
    CHECK(!linked_server(list, ""));
    CHECK(unlink_network(list, 1, "Home"));
    CHECK(!unlink_network(list, 1, "Home"));
    CHECK(!linked_server(list, "Home"));
}

TEST(choosing_a_server) {
    ServerList list;
    CHECK(choose_server(list, "Home").reason == ChoiceReason::NoServers);
    CHECK(!choose_server(list, "Home").index);

    remember_server(list, server("https://home.lan"));
    auto only = choose_server(list, "Cafe");
    CHECK(only.index == std::size_t{0} && only.reason == ChoiceReason::OnlyServer);

    remember_server(list, server("https://friend.lan"));  // now the most recent
    link_network(list, 0, "Home");
    link_network(list, 1, "Friend's Wi-Fi");
    auto home = choose_server(list, "Home");
    CHECK(home.index == std::size_t{0} && home.reason == ChoiceReason::Network);
    auto away = choose_server(list, "Friend's Wi-Fi");
    CHECK(away.index == std::size_t{1} && away.reason == ChoiceReason::Network);

    auto elsewhere = choose_server(list, "Cafe");  // not linked: the last used
    CHECK(elsewhere.index == std::size_t{1} && elsewhere.reason == ChoiceReason::LastUsed);
    mark_used(list, 0);
    CHECK(choose_server(list, "Cafe").index == std::size_t{0});
    CHECK(choose_server(list, "").reason == ChoiceReason::LastUsed);  // network unknown
}
