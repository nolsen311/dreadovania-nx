// SPDX-License-Identifier: GPL-3.0-or-later
// Download checks and install planning. tests/data/server.tar and its manifest
// were written by dreadovania-server's own mods.iter_mod_tar / mod_manifest.
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "check.hpp"
#include "dreadovania/install.hpp"
#include "dreadovania/sha256.hpp"

using namespace dreadovania;

namespace {

std::string read(const std::string& name) {
    std::ifstream in(std::string(TEST_DATA_DIR) + "/" + name, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

ModManifest server_manifest() {
    std::string error;
    auto m = parse_mod_manifest(read("server-manifest.json"), error);
    CHECK(m.has_value());
    return m ? *m : ModManifest{};
}

// Feeds the archive in pieces of `step` bytes; returns the files written.
std::map<std::string, std::string> run(TarVerifier& v, const std::string& tar, std::size_t step,
                                      bool* ok) {
    std::map<std::string, std::string> out;
    std::string current;
    v.on_file_start = [&](const ModFile& f) { current = f.sd_path; out[current]; return true; };
    v.on_data = [&](const char* d, std::size_t n) { out[current].append(d, n); return true; };
    v.on_file_end = [&](const ModFile&) { return true; };
    *ok = true;
    for (std::size_t i = 0; i < tar.size() && *ok; i += step) {
        *ok = v.feed(tar.data() + i, std::min(step, tar.size() - i));
    }
    return out;
}

std::string with_byte(std::string s, std::size_t at, char c) {
    s[at] = c;
    return s;
}

}  // namespace

TEST(sha256_known_answers) {
    Sha256 empty;
    CHECK(empty.hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Sha256 abc;
    abc.update("abc");
    CHECK(abc.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Sha256 two_blocks;
    two_blocks.update("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    CHECK(two_blocks.hex() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 million;
    std::string a(1000, 'a');
    for (int i = 0; i < 1000; ++i) {
        million.update(a);
    }
    CHECK(million.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(the_servers_tar_is_read_in_any_chunk_size) {
    ModManifest m = server_manifest();
    std::string tar = read("server.tar");
    CHECK(tar.size() == m.tar_bytes);
    for (std::size_t step : {1u, 7u, 512u, 513u, 4096u, 1u << 20}) {
        TarVerifier v(m);
        bool ok = false;
        auto files = run(v, tar, step, &ok);
        CHECK(ok);
        CHECK(v.complete());
        CHECK(files.size() == 4);
        CHECK(files["/atmosphere/exefs_patches/DreadRandovania/ABCDEF.ips"] == "IPS");
        CHECK(files["/atmosphere/contents/010093801237c000/exefs/main.npdm"].empty());
        CHECK(files["/atmosphere/contents/010093801237c000/romfs/actors/characters/"
                    "chozocommander/charclasses/chozocommander.bmsad"] == std::string(700, 'A'));
    }
}

TEST(damaged_or_unexpected_downloads_are_refused) {
    ModManifest m = server_manifest();
    std::string tar = read("server.tar");
    struct Case {
        std::string data;
        const char* expect;
    };
    std::size_t data_at = 512;  // the first file's contents follow its header
    Case cases[] = {
        {with_byte(tar, data_at + 10, 'B'), "damaged"},
        {with_byte(tar, 5, 'X'), "bad tar header"},
        {tar.substr(0, tar.size() - 1024), ""},     // no end marker: incomplete
        {tar.substr(0, 512 * 3), ""},               // cut off mid-way: incomplete
        {std::string(1024, '\0'), "ended before every file"},
        {tar + std::string(512, 'x'), "after the end"},
    };
    for (const Case& c : cases) {
        TarVerifier v(m);
        bool ok = false;
        run(v, c.data, 300, &ok);
        CHECK(!v.complete());
        if (*c.expect != '\0') {
            CHECK(!ok);
            CHECK(v.error().find(c.expect) != std::string::npos);
        }
    }
    // The archive must hold exactly the listed files, in order.
    ModManifest fewer = m;
    fewer.files.pop_back();
    TarVerifier extra(fewer);
    bool ok = true;
    run(extra, tar, 512, &ok);
    CHECK(!ok);
    CHECK(extra.error().find("files the server's list doesn't") != std::string::npos);
    ModManifest swapped = m;
    std::swap(swapped.files[0], swapped.files[1]);
    TarVerifier order(swapped);
    run(order, tar, 512, &ok);
    CHECK(!ok);
    CHECK(order.error().find("doesn't match") != std::string::npos);
}

TEST(a_write_failure_stops_the_download) {
    ModManifest m = server_manifest();
    TarVerifier v(m);
    v.on_file_start = [](const ModFile&) { return true; };
    v.on_data = [](const char*, std::size_t) { return false; };  // e.g. SD card full
    std::string tar = read("server.tar");
    CHECK(!v.feed(tar.data(), tar.size()));
    CHECK(v.error().find("couldn't write") != std::string::npos);
}

TEST(the_manifest_only_allows_dreads_mod_folders) {
    std::string error;
    auto bad = [&](const char* path) {
        std::string body = std::string(R"({"tar_bytes": 2048, "files": [{"path": ")") + path +
                           R"(", "size": 1, "sha256": ")" + std::string(64, 'a') + "\"}]}";
        return !parse_mod_manifest(body, error);
    };
    CHECK(bad("atmosphere/config/system_settings.ini"));
    CHECK(bad("atmosphere/contents/010093801237c000/../../hbmenu.nro"));
    CHECK(bad("atmosphere/contents/0100000000001000/romfs/x"));
    CHECK(bad("atmosphere//contents/010093801237c000/romfs/x"));
    CHECK(!bad("atmosphere/contents/010093801237c000/romfs/x"));
    CHECK(!parse_mod_manifest(R"({"tar_bytes": 2048, "files": []})", error));
    CHECK(!parse_mod_manifest("[]", error));
}

TEST(install_plans_keep_other_mods) {
    ModManifest m = server_manifest();
    std::vector<std::string> owned = {
        "/atmosphere/exefs_patches/DreadRandovania/ABCDEF.ips",           // replaced
        "/atmosphere/contents/010093801237c000/romfs/old/gone.bin",        // removed
        "/atmosphere/config/system_settings.ini",                          // never touched
    };
    const std::string theirs = "/atmosphere/contents/010093801237c000/exefs/main.npdm";
    auto exists = [&](const std::string& p) {
        return p == theirs || p == "/atmosphere/exefs_patches/DreadRandovania/ABCDEF.ips";
    };
    InstallPlan plan = plan_install(m, owned, exists);
    CHECK(plan.files.size() == 4);
    CHECK(plan.conflicts == std::vector<std::string>{theirs});
    CHECK(plan.remove ==
          std::vector<std::string>{"/atmosphere/contents/010093801237c000/romfs/old/gone.bin"});
    CHECK(plan.record.size() == 5);  // what was ours, plus what will be
    for (const std::string& p : plan.record) {
        CHECK(p != "/atmosphere/config/system_settings.ini");
    }
}

TEST(sd_manifests_round_trip_with_the_servers_format) {
    std::vector<std::string> files = {"/atmosphere/contents/010093801237c000/romfs/a"};
    std::string text = sd_manifest_json(files, "seed-1", "installed");
    CHECK(parse_sd_manifest(text) == files);
    CHECK(sd_manifest_seed(text) == "seed-1");
    CHECK(!sd_manifest_seed(sd_manifest_json(files, "seed-1", "installing")));
    // As the server writes it (push.py, json.dumps indent=1), with a hostile entry.
    std::string server = R"({"tool": "dreadovania", "seed_id": "s2", "state": "installed",
      "files": ["/atmosphere/contents/010093801237c000/romfs/a", "/atmosphere/config/x.ini",
                "/atmosphere/contents/010093801237c000/../../../hbmenu.nro"]})";
    CHECK(parse_sd_manifest(server) == files);
    CHECK(parse_sd_manifest("garbage").empty());
}

TEST(empty_folders_are_tidied_but_shared_ones_kept) {
    auto dirs = folders_to_tidy({"/atmosphere/contents/010093801237c000/romfs/a/b.bin",
                                 "/atmosphere/exefs_patches/DreadRandovania/x.ips"});
    CHECK(dirs == (std::vector<std::string>{
                      "/atmosphere/contents/010093801237c000/romfs/a",
                      "/atmosphere/contents/010093801237c000/romfs",
                      "/atmosphere/contents/010093801237c000",
                      "/atmosphere/exefs_patches/DreadRandovania",
                  }));
}
