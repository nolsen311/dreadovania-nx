// SPDX-License-Identifier: GPL-3.0-or-later
// Responses are shaped like dreadovania-server's (docs/openapi.json).
#include <fstream>
#include <iterator>
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
       "mod": {"status": "ready", "file_count": 12, "job_id": "j2"}, "race": false,
       "job_id": "j1"},
      {"id": "s2", "status": "pending", "preset_name": null, "source": "permalink",
       "created_at": "2026-11-09T21:00:00+00:00", "seed_hash": null, "mod": null}
    ])";
    auto seeds = parse_seed_list(body);
    CHECK(seeds && seeds->size() == 2);
    CHECK((*seeds)[0].label == "Starter preset");
    CHECK((*seeds)[0].mod_status == "ready");
    CHECK((*seeds)[1].label == "Permalink");
    CHECK((*seeds)[1].mod_status.empty());
    CHECK((*seeds)[0].job_id == "j1" && (*seeds)[0].mod_job_id == "j2");
    CHECK((*seeds)[1].job_id.empty() && (*seeds)[1].mod_job_id.empty());
    CHECK(describe_seed((*seeds)[0], 80) ==
          "2026-11-09  Starter preset  ABCD EFGH  [patched]");
    CHECK(describe_seed((*seeds)[1], 80) == "2026-11-09  Permalink  [pending]");
    CHECK(parse_seed_list("[]")->empty());
    CHECK(!parse_seed_list(R"([{"id": "x"}])"));
    CHECK(!parse_seed_list("{}"));
}

TEST(long_seed_lines_are_cut_to_the_screen) {
    SeedSummary seed{"s", "ready", std::string(100, 'x'), "2026-11-09", "", "", "", ""};
    std::string line = describe_seed(seed, 40);
    CHECK(line.size() == 40);
    CHECK(line.find("...  [not patched]") != std::string::npos);
    CHECK(describe_seed(seed, 5).size() <= 5);
}

TEST(presets_and_jobs) {
    auto presets = parse_preset_list(R"([
      {"id": "starter_preset", "name": "Starter preset", "description": "A good start",
       "builtin": true},
      {"id": "p2", "name": "", "description": "", "builtin": false}
    ])");
    CHECK(presets && presets->size() == 2);
    CHECK((*presets)[0].name == "Starter preset" && (*presets)[0].description == "A good start");
    CHECK((*presets)[1].name == "p2");  // never a blank line
    CHECK(!parse_preset_list(R"([{"name": "no id"}])"));
    CHECK(!parse_preset_list("{}"));

    CHECK(generate_request_body("a\"b", false) == R"({"preset_id": "a\"b", "race": false})");
    auto sub = parse_submitted(R"({"seed_id": "s1", "job_id": "j1"})");
    CHECK(sub && sub->seed_id == "s1" && sub->job_id == "j1");
    CHECK(!parse_submitted(R"({"seed_id": "s1"})"));

    auto job = parse_job(R"({"status": "running", "progress": "Placing items", "error": null})");
    CHECK(job && job->progress == "Placing items" && job->error.empty() && !job->finished());
    CHECK(parse_job(R"({"status": "failed", "error": "boom"})")->finished());
    CHECK(!parse_job(R"({"progress": "x"})"));
}

TEST(seed_chips_and_dates) {
    SeedSummary seed{"s", "ready", "Starter", "2026-11-09T20:00:00+00:00", "", "ready", "", ""};
    CHECK(seed_state(seed, false).label == "Patched" && seed_state(seed, false).tone == Tone::Ok);
    CHECK(seed_state(seed, true).label == "Installed");
    seed.mod_status = "";
    CHECK(seed_state(seed, false).label == "Not patched");
    seed.mod_status = "pending";
    CHECK(seed_state(seed, false).tone == Tone::Wait);
    seed.status = "pending";
    CHECK(seed_state(seed, false).label == "Generating");
    seed.status = "failed";
    CHECK(seed_state(seed, false).tone == Tone::Bad);
    CHECK(short_date("2026-11-09T20:00:00+00:00") == "Nov 9");
    CHECK(short_date("2026-01-31") == "Jan 31");
    CHECK(short_date("2026-13-01").empty());
    CHECK(short_date("soon").empty());
}

TEST(preset_descriptions_as_plain_text) {
    CHECK(plain_text("One.<br/><br/>Two &amp; <b>three</b>") == "One.\n\nTwo & three");
    CHECK(plain_text("a<BR>b<br />c") == "a\nb\nc");
    CHECK(plain_text("unclosed <tag") == "unclosed");  // trailing space trimmed
    CHECK(plain_text("x &unknown; y") == "x &unknown; y");
}

TEST(installing_waits_while_dread_runs) {
    CHECK(!install_blocker(true, std::nullopt));
    CHECK(!install_blocker(false, std::nullopt));  // applet mode, no game running
    CHECK(!install_blocker(false, 0x0100000000010000ULL));  // another game suspended
    auto blocked = install_blocker(false, kDreadProgramId);
    CHECK(blocked && blocked->find("Close it first") != std::string::npos);
    // Title takeover over Dread: the app *is* the application; Dread isn't running.
    CHECK(!install_blocker(true, kDreadProgramId));
}

TEST(app_versions_and_updates) {
    CHECK(parse_version("0.4.2") == (std::array<int, 3>{0, 4, 2}));
    CHECK(parse_version("v1.10.0") == (std::array<int, 3>{1, 10, 0}));
    CHECK(!parse_version("1.0"));
    CHECK(!parse_version("1.0.0-beta"));
    CHECK(!parse_version(""));
    CHECK(compare_versions("0.4.10", "0.4.9") > 0);  // numbers, not text
    CHECK(compare_versions("0.4.2", "v0.4.2") == 0);
    CHECK(compare_versions("0.5.0", "0.4.9") > 0);

    auto latest = parse_app_latest(
        R"({"available": true, "version": "0.4.3", "size": 9438743, "sha256": ")" +
        std::string(64, 'a') + R"(", "minimum_version": "0.2.0", "reason": null})");
    CHECK(latest && latest->available && latest->version == "0.4.3" && latest->size == 9438743);
    CHECK(update_need("0.4.2", *latest) == UpdateNeed::Available);
    CHECK(update_need("0.4.3", *latest) == UpdateNeed::None);
    CHECK(update_need("0.5.0", *latest) == UpdateNeed::None);  // never "update" to older
    CHECK(update_need("0.1.0", *latest) == UpdateNeed::Required);

    auto off = parse_app_latest(R"({"available": false, "minimum_version": "0.2.0",
                                    "reason": "app updates are turned off on this server"})");
    CHECK(off && !off->available && off->reason.find("turned off") != std::string::npos);
    CHECK(update_need("0.4.2", *off) == UpdateNeed::None);
    CHECK(update_need("0.1.9", *off) == UpdateNeed::Required);  // still told it's too old
    CHECK(!parse_app_latest(R"({"available": true, "version": "0.4.3"})"));  // no checksum
    CHECK(!parse_app_latest("[]"));
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

TEST(untrusted_after_mozilla_roots_says_so) {
    std::string both = untrusted_certificate_message("", UserCa::Missing, "", true);
    CHECK(both.find("by the Switch or by Mozilla's public roots") != std::string::npos);
    CHECK(untrusted_certificate_message("", UserCa::Missing, "").find("Mozilla") ==
          std::string::npos);
}

TEST(client_certificate_messages_say_what_to_do) {
    std::string none = client_certificate_message("verify 0x2867B, RSA cert", false);
    CHECK(none.find("asked for a client certificate") != std::string::npos);
    CHECK(none.find("(verify 0x2867B, RSA cert)") != std::string::npos);
    CHECK(none.find("mTLS") != std::string::npos);
    CHECK(none.find("isn't trusted") == std::string::npos);
    std::string refused = client_certificate_message("", true);
    CHECK(refused.find("didn't accept this Switch's client certificate") != std::string::npos);
    CHECK(refused.find("Pair this Switch again") != std::string::npos);
    CHECK(refused.find("()") == std::string::npos);
}

TEST(attempts_summary_lists_each_or_says_once) {
    CHECK(attempts_summary({}).empty());
    CHECK(attempts_summary({{"Switch", "not trusted"}}).empty());
    CHECK(attempts_summary({{"Switch", "not trusted (a)"}, {"own CA, RSA", "cipher refused"}}) ==
          ". Tried: Switch: not trusted (a); own CA, RSA: cipher refused");
    CHECK(attempts_summary({{"Switch", "x"}, {"own CA, RSA", "x"}, {"roots 1", "x"}}) ==
          ". Tried: Switch; own CA, RSA; roots 1. All: x");
}

namespace {

std::string labelled(const std::string& label, const std::string& body) {
    return "\n# Issuer: CN=" + label + "\n# Label: \"" + label + "\"\n# Serial: 1\n" +
           "-----BEGIN CERTIFICATE-----\n" + body + "\n-----END CERTIFICATE-----\n";
}

}  // namespace

TEST(roots_come_in_batches_most_used_first) {
    std::string bundle = labelled("Old Root A", "AAA") + labelled("ISRG Root X2", "X2") +
                         labelled("Old Root B", "BBB") + labelled("ISRG Root X1", "X1") +
                         "-----BEGIN CERTIFICATE-----\nNOLABEL\n-----END CERTIFICATE-----\n" +
                         labelled("Old Root C", "CCC") + "-----BEGIN CERTIFICATE-----\ncut";
    auto batches = root_batches(bundle, 2);
    CHECK(batches.size() == 3);  // 6 complete certificates, the truncated one ignored
    // X1 then X2 (the priority order), then the rest in the bundle's order.
    CHECK(batches[0][0].find("X1") != std::string::npos);
    CHECK(batches[0][1].find("X2") != std::string::npos);
    CHECK(batches[1][0].find("AAA") != std::string::npos);
    CHECK(batches[1][1].find("BBB") != std::string::npos);
    CHECK(batches[2][0].find("NOLABEL") != std::string::npos);
    CHECK(batches[2][1].find("CCC") != std::string::npos);
    CHECK(batches[0][0].rfind("-----BEGIN CERTIFICATE-----", 0) == 0);
    CHECK(batches[2][1].find("-----END CERTIFICATE-----") != std::string::npos);
    CHECK(root_batches(bundle, 0).empty());
    CHECK(root_batches("", 32).empty());
}

TEST(the_bundled_roots_fit_the_batches) {
    std::ifstream in(ROOTS_FILE, std::ios::binary);
    std::string bundle((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(!bundle.empty());
    auto batches = root_batches(bundle, 32);
    std::size_t total = 0;
    for (const auto& batch : batches) {
        CHECK(!batch.empty() && batch.size() <= 32);
        total += batch.size();
    }
    CHECK(total == split_pem_certificates(bundle).size());  // none lost or doubled
    // Every priority root is still in the bundle and lands in the first batch. If a
    // certifi update renames or drops one, this fails: update kPriorityRoots.
    CHECK(priority_root_labels().size() <= 32);  // they all fit the first batch
    for (std::string_view label : priority_root_labels()) {
        std::size_t at = bundle.find("# Label: \"" + std::string(label) + "\"\n");
        CHECK(at != std::string::npos);
        std::size_t begin = bundle.find("-----BEGIN CERTIFICATE-----", at);
        std::size_t end = bundle.find("-----END CERTIFICATE-----", begin);
        std::string pem = bundle.substr(begin, end + 25 - begin);
        bool first = false;
        for (const auto& cert : batches[0]) {
            first = first || cert == pem;
        }
        CHECK(first);
    }
}

TEST(origins_of_urls) {
    CHECK(origin_of("https://Dread.Example.com/api/v1/seeds?x=1") ==
          "https://dread.example.com");
    CHECK(origin_of("HTTPS://dread.example.com:8443") == "https://dread.example.com:8443");
    CHECK(origin_of("https://[::1]:8080/x") == "https://[::1]:8080");
    CHECK(origin_of("https://a.example#frag") == "https://a.example");
    CHECK(origin_of("http://dread.example.com/").empty());  // never plain HTTP
    CHECK(origin_of("https://").empty());
    CHECK(origin_of("https:///path").empty());
    CHECK(origin_of("https://user@host/").empty());
    CHECK(origin_of("dread.example.com").empty());
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
