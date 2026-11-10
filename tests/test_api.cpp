// SPDX-License-Identifier: GPL-3.0-or-later
// Responses are shaped like dreadovania-server's (docs/openapi.json).
#include <string>

#include "check.hpp"
#include "dreadovania/api.hpp"

using namespace dreadovania;

TEST(server_addresses) {
    CHECK(normalize_server_url("dread.example.com").origin == "https://dread.example.com");
    CHECK(normalize_server_url(" HTTPS://Dread.Example.com/ ").origin ==
          "https://dread.example.com");
    CHECK(normalize_server_url("https://dread.lan:8443").origin == "https://dread.lan:8443");
    CHECK(normalize_server_url("https://dread.lan:443").origin == "https://dread.lan");
    CHECK(!normalize_server_url("http://dread.lan").origin);
    CHECK(normalize_server_url("http://dread.lan").error.find("https") != std::string::npos);
    CHECK(!normalize_server_url("ftp://dread.lan").origin);
    CHECK(!normalize_server_url("https://dread.lan/path").origin);
    CHECK(!normalize_server_url("https://user@dread.lan").origin);
    CHECK(!normalize_server_url("https://dread.lan:99999").origin);
    CHECK(!normalize_server_url("https://dread.lan:x").origin);
    CHECK(!normalize_server_url("https://dread lan").origin);
    CHECK(!normalize_server_url("https://").origin);
    CHECK(!normalize_server_url("https://.lan").origin);
    CHECK(!normalize_server_url("https://a..lan").origin);
}

TEST(pairing_codes) {
    CHECK(normalize_pairing_code("abcd-efgh") == "ABCDEFGH");
    CHECK(normalize_pairing_code(" PMRR XYW9 ") == "PMRRXYW9");
    CHECK(!normalize_pairing_code("ABCD-EFG"));
    CHECK(!normalize_pairing_code("ABCD-EFGHI"));
    CHECK(!normalize_pairing_code("0000-0000"));  // 0 and O aren't used
    CHECK(!normalize_pairing_code("ABCD-EFG1"));
}

TEST(device_config_round_trips) {
    DeviceConfig config{"https://dread.lan", "dev-1", "Nate's \"OG\" Switch", "dvt_secret"};
    auto back = parse_device_config(serialize_device_config(config));
    CHECK(back.has_value());
    CHECK(back->server == config.server);
    CHECK(back->name == config.name);
    CHECK(back->token == config.token);
    CHECK(!parse_device_config("{}"));
    CHECK(!parse_device_config(R"({"server": "http://dread.lan", "token": "t"})"));
    CHECK(!parse_device_config(R"({"server": "https://dread.lan", "token": ""})"));
    CHECK(!parse_device_config("not json"));
}

TEST(pair_request_and_response) {
    CHECK(pair_request_body("ABCDEFGH", "My \"Switch\"") ==
          R"({"code": "ABCDEFGH", "name": "My \"Switch\""})");
    auto paired = parse_paired(R"({"device_id": "d1", "name": "S", "token": "dvt_x"})");
    CHECK(paired && paired->token == "dvt_x" && paired->device_id == "d1");
    CHECK(!parse_paired(R"({"device_id": "d1"})"));
    CHECK(!parse_paired("[]"));
}

TEST(seed_lists) {
    const char* body = R"([
      {"id": "s1", "status": "ready", "preset_name": "Starter preset", "source": "preset",
       "created_at": "2026-11-09T20:00:00+00:00", "seed_hash": "ABCD EFGH",
       "mod": {"status": "ready", "file_count": 12}, "race": false},
      {"id": "s2", "status": "pending", "preset_name": null, "source": "permalink",
       "created_at": "2026-11-09T21:00:00+00:00", "seed_hash": null, "mod": null}
    ])";
    auto seeds = parse_seed_list(body);
    CHECK(seeds && seeds->size() == 2);
    CHECK((*seeds)[0].label == "Starter preset");
    CHECK((*seeds)[0].mod_status == "ready");
    CHECK((*seeds)[1].label == "Permalink");
    CHECK((*seeds)[1].mod_status.empty());
    CHECK(describe_seed((*seeds)[0], 80) ==
          "2026-11-09  Starter preset  ABCD EFGH  [patched]");
    CHECK(describe_seed((*seeds)[1], 80) == "2026-11-09  Permalink  [pending]");
    CHECK(parse_seed_list("[]")->empty());
    CHECK(!parse_seed_list(R"([{"id": "x"}])"));
    CHECK(!parse_seed_list("{}"));
}

TEST(long_seed_lines_are_cut_to_the_screen) {
    SeedSummary seed{"s", "ready", std::string(100, 'x'), "2026-11-09", "", ""};
    std::string line = describe_seed(seed, 40);
    CHECK(line.size() == 40);
    CHECK(line.find("...  [not patched]") != std::string::npos);
    CHECK(describe_seed(seed, 5).size() <= 5);
}

TEST(problem_details) {
    CHECK(problem_detail(R"({"title": "Pairing failed", "detail": "code expired"})") ==
          "code expired");
    CHECK(problem_detail(R"({"title": "Not found"})") == "Not found");
    CHECK(!problem_detail("<html>"));
}

TEST(user_ca_files) {
    CHECK(classify_user_ca("", false) == UserCa::Missing);
    CHECK(classify_user_ca("-----BEGIN CERTIFICATE-----\nMIIF...", true) == UserCa::Loaded);
    CHECK(classify_user_ca("\x30\x82\x05\xca binary DER", true) == UserCa::NotPem);
}

TEST(untrusted_certificate_messages_say_what_to_check) {
    std::string loaded = untrusted_certificate_message(
        "The certificate validity starts in the future", UserCa::Loaded, "2026-11-09 22:10");
    CHECK(loaded.find("(The certificate validity starts in the future)") != std::string::npos);
    CHECK(loaded.find("Your CA from /config/dreadovania/ca.pem was used") != std::string::npos);
    CHECK(loaded.find("it says 2026-11-09 22:10 UTC") != std::string::npos);
    std::string missing = untrusted_certificate_message("", UserCa::Missing, "");
    CHECK(missing.find("copy its certificate to /config/dreadovania/ca.pem") !=
          std::string::npos);
    CHECK(missing.find("()") == std::string::npos);
    CHECK(missing.find("it says") == std::string::npos);
    CHECK(untrusted_certificate_message("x", UserCa::NotPem, "t").find("isn't a PEM") !=
          std::string::npos);
}

TEST(pem_files_are_split_one_certificate_each) {
    const std::string one = "-----BEGIN CERTIFICATE-----\nAAA\n-----END CERTIFICATE-----";
    const std::string two = "-----BEGIN CERTIFICATE-----\nBBB\n-----END CERTIFICATE-----";
    auto certs = split_pem_certificates("subject=x\r\n" + one + "\r\n\nnoise\n" + two + "\n");
    CHECK(certs.size() == 2);
    CHECK(certs[0] == one + "\n");
    CHECK(certs[1] == two + "\n");
    CHECK(split_pem_certificates("").empty());
    CHECK(split_pem_certificates("-----BEGIN CERTIFICATE-----\ntruncated").empty());
    CHECK(split_pem_certificates(one + "\n-----BEGIN CERTIFICATE-----\nX").size() == 1);
}
