// SPDX-License-Identifier: GPL-3.0-or-later
// source/updater.cpp on the host, against a temporary "SD card" and the fake server.
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "check.hpp"
#include "dreadovania/sha256.hpp"
#include "updater.hpp"

namespace fs = std::filesystem;

namespace {

const std::string kSelf = "sdmc:/switch/dreadovania.nro";
const std::string kUrl = "https://dread.test/api/v1/app/dreadovania.nro";

std::string read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct Card {
    fs::path old_cwd = fs::current_path();
    fs::path dir;
    Card() {
        dir = fs::temp_directory_path() / ("dreadovania-update-" + std::to_string(getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir / "sdmc:/switch");
        fs::current_path(dir);
        std::ofstream(kSelf, std::ios::binary) << "old app";
    }
    ~Card() {
        fs::current_path(old_cwd);
        fs::remove_all(dir);
    }
};

dreadovania::AppLatest latest_for(const std::string& nro) {
    dreadovania::Sha256 h;
    h.update(nro);
    dreadovania::AppLatest l;
    l.available = true;
    l.version = "0.4.3";
    l.size = nro.size();
    l.sha256 = h.hex();
    return l;
}

app::UpdateResult update(app::Http& http, const dreadovania::AppLatest& latest,
                         const std::string& self = kSelf) {
    return app::download_update(http, "https://dread.test", "dvt_x", latest, self,
                                [](double) {});
}

}  // namespace

TEST(an_update_replaces_the_app) {
    Card card;
    const std::string nro(5000, 'N');
    app::Http http;
    http.routes[kUrl] = {200, nro, ""};
    auto r = update(http, latest_for(nro));
    CHECK(r.ok);
    CHECK(read(kSelf) == nro);
    CHECK(!fs::exists(kSelf + ".new") && !fs::exists(kSelf + ".old"));
}

TEST(a_bad_download_leaves_the_app_alone) {
    Card card;
    const std::string nro(5000, 'N');
    app::Http http;
    http.routes[kUrl] = {200, std::string(5000, 'X'), ""};  // wrong bytes
    auto r = update(http, latest_for(nro));
    CHECK(!r.ok && r.message.find("checksum") != std::string::npos);
    CHECK(read(kSelf) == "old app");
    CHECK(!fs::exists(kSelf + ".new"));

    http.routes[kUrl] = {200, nro + "extra", ""};  // longer than announced
    r = update(http, latest_for(nro));
    CHECK(!r.ok && r.message.find("expected size") != std::string::npos);
    CHECK(read(kSelf) == "old app");

    http.routes[kUrl] = {404, R"({"detail": "No app release available"})", ""};
    r = update(http, latest_for(nro));
    CHECK(!r.ok && r.message.find("No app release available") != std::string::npos);
    CHECK(read(kSelf) == "old app");
}

TEST(only_an_nro_on_the_sd_card_updates_itself) {
    CHECK(app::updatable_path("sdmc:/switch/dreadovania.nro"));
    CHECK(app::updatable_path("sdmc:/switch/dreadovania/dreadovania.nro"));
    CHECK(!app::updatable_path(""));
    CHECK(!app::updatable_path("dreadovania.nro"));  // e.g. started over the network
    CHECK(!app::updatable_path("sdmc:/switch/dreadovania.nro.bak"));
    app::Http http;
    auto r = update(http, latest_for("x"), "nxlink:/dreadovania.nro");
    CHECK(!r.ok && r.message.find("can't update itself") != std::string::npos);
    CHECK(http.calls.empty());
}
