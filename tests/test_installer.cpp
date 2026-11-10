// SPDX-License-Identifier: GPL-3.0-or-later
// source/installer.cpp, run on the host against a folder named "sdmc:" in a
// temporary directory, with the server's real tar and manifest (tests/data).
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "check.hpp"
#include "dreadovania/install.hpp"
#include "installer.hpp"

namespace fs = std::filesystem;

namespace {

const std::string kDread = "sdmc:/atmosphere/contents/010093801237c000";
const std::string kBase = "https://dread.test/api/v1/seeds/s1";

std::string read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write(const std::string& path, const std::string& data) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << data;
}

struct Card {  // a temporary "SD card" in the working directory
    fs::path old_cwd = fs::current_path();
    fs::path dir;
    Card() {
        dir = fs::temp_directory_path() / ("dreadovania-test-" + std::to_string(getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir / "sdmc:");
        fs::current_path(dir);
    }
    ~Card() {
        fs::current_path(old_cwd);
        fs::remove_all(dir);
    }
};

app::Http server() {
    app::Http http;
    http.routes[kBase + "/mod/manifest"] = {200, read(std::string(TEST_DATA_DIR) + "/server-manifest.json"), ""};
    http.routes[kBase + "/mod.tar"] = {200, read(std::string(TEST_DATA_DIR) + "/server.tar"), ""};
    return http;
}

app::InstallResult install(app::Http& http, bool overwrite) {
    return app::install_seed(http, "https://dread.test", "dvt_x", "s1", overwrite,
                             [](const std::string&) {});
}

}  // namespace

TEST(installer_replaces_the_previous_seed_and_keeps_other_mods) {
    Card card;
    // The previous seed (recorded on the card) and another mod's files.
    write(kDread + "/romfs/old/gone.bin", "old seed");
    write(kDread + "/romfs/theirs.bin", "another mod");
    write("sdmc:/atmosphere/contents/0100000000001000/romfs/x", "a different game");
    write("sdmc:/config/dreadovania/installed.json",
          dreadovania::sd_manifest_json({"/atmosphere/contents/010093801237c000/romfs/old/gone.bin"},
                                        "s0", "installed"));
    app::Http http = server();
    app::InstallResult result = install(http, false);
    CHECK(result.installed);
    CHECK(read(kDread + "/romfs/actors/characters/chozocommander/charclasses/chozocommander.bmsad") ==
          std::string(700, 'A'));
    CHECK(read("sdmc:/atmosphere/exefs_patches/DreadRandovania/ABCDEF.ips") == "IPS");
    CHECK(fs::exists(kDread + "/exefs/main.npdm"));
    CHECK(!fs::exists(kDread + "/romfs/old/gone.bin"));
    CHECK(!fs::exists(kDread + "/romfs/old"));  // emptied folder tidied
    CHECK(read(kDread + "/romfs/theirs.bin") == "another mod");
    CHECK(fs::exists("sdmc:/atmosphere/contents/0100000000001000/romfs/x"));
    CHECK(!fs::exists("sdmc:/config/dreadovania/staging"));
    std::string record = read("sdmc:/config/dreadovania/installed.json");
    CHECK(dreadovania::sd_manifest_seed(record) == "s1");
    CHECK(dreadovania::parse_sd_manifest(record).size() == 4);
    CHECK(app::installed_seed() == "s1");
}

TEST(installer_asks_before_replacing_another_mods_file) {
    Card card;
    write("sdmc:/atmosphere/exefs_patches/DreadRandovania/ABCDEF.ips", "theirs");
    app::Http http = server();
    app::InstallResult first = install(http, false);
    CHECK(!first.installed);
    CHECK(first.conflicts.size() == 1);
    CHECK(read("sdmc:/atmosphere/exefs_patches/DreadRandovania/ABCDEF.ips") == "theirs");
    CHECK(!fs::exists("sdmc:/config/dreadovania/installed.json"));
    app::InstallResult second = install(http, true);
    CHECK(second.installed);
    CHECK(read("sdmc:/atmosphere/exefs_patches/DreadRandovania/ABCDEF.ips") == "IPS");
}

TEST(installer_leaves_the_card_alone_when_the_download_is_bad) {
    Card card;
    write(kDread + "/romfs/keep.bin", "untouched");
    app::Http http = server();
    std::string& tar = http.routes[kBase + "/mod.tar"].body;
    tar[512 + 10] = 'B';  // corrupt the first file's contents
    app::InstallResult result = install(http, false);
    CHECK(!result.installed);
    CHECK(result.message.find("damaged") != std::string::npos);
    CHECK(result.message.find("Nothing on the SD card was changed") != std::string::npos);
    CHECK(read(kDread + "/romfs/keep.bin") == "untouched");
    CHECK(!fs::exists("sdmc:/config/dreadovania/installed.json"));
    CHECK(!fs::exists("sdmc:/config/dreadovania/staging"));
    CHECK(!fs::exists("sdmc:/atmosphere/exefs_patches"));
}

TEST(installer_reports_the_servers_refusal) {
    Card card;
    app::Http http;
    http.routes[kBase + "/mod/manifest"] = {409, R"({"title": "Not patched", "detail": "patch this seed first"})", ""};
    app::InstallResult result = install(http, false);
    CHECK(!result.installed);
    CHECK(result.message == "Not installed: patch this seed first (HTTP 409)");
}
