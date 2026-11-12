// SPDX-License-Identifier: GPL-3.0-or-later
// Dreadovania for Switch (ADR 0004): pair with a Dreadovania server, list its
// seeds, generate and patch new ones there, and install them. Drawn with SDL2
// (ADR 0009, docs/design).
#include <switch.h>

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "dreadovania/api.hpp"
#include "dreadovania/certs.hpp"
#include "dreadovania/servers.hpp"
#include "dreadovania/version.hpp"
#include "http.hpp"
#include "installer.hpp"
#include "jobs.hpp"
#include "system.hpp"
#include "updater.hpp"
#include "ui/gfx.hpp"
#include "ui/screens.hpp"
#include "ui/widgets.hpp"

namespace {

using dreadovania::DeviceConfig;
using dreadovania::PairedServer;
using dreadovania::ServerList;
using dreadovania::PresetSummary;
using dreadovania::SeedSummary;

constexpr const char* kDeviceFile = "sdmc:/config/dreadovania/device.json";    // before 0.6
constexpr const char* kServersFile = "sdmc:/config/dreadovania/servers.json";  // ADR 0010

// ---- settings on the SD card -------------------------------------------------

std::optional<std::string> read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool save_servers(const ServerList& list) {
    const std::string tmp = std::string(kServersFile) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << dreadovania::serialize_server_list(list);
        if (!out) {
            return false;
        }
    }
    std::remove(kServersFile);
    return std::rename(tmp.c_str(), kServersFile) == 0;
}

// ---- client certificates (server ADR 0023) ------------------------------------

std::string on_sd(const std::string& path) { return "sdmc:" + path; }

// Write a server's .p12 next to the others, replacing the old one only once the new
// one is fully written.
bool save_certificate(const std::string& device_id, const std::string& p12) {
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/dreadovania", 0777);
    mkdir("sdmc:/config/dreadovania/certificates", 0777);
    const std::string path = on_sd(dreadovania::certificate_file(device_id));
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(p12.data(), static_cast<std::streamsize>(p12.size()));
        if (!out) {
            return false;
        }
    }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

void remove_certificate(const std::string& device_id) {
    if (!device_id.empty()) {
        std::remove(on_sd(dreadovania::certificate_file(device_id)).c_str());
    }
}

// The certificate to present to a server: one from the owner's own CA, if they put
// <host>.p12 and <host>.password on the SD card; otherwise the server's own.
std::pair<std::string, std::string> certificate_for(const PairedServer& server) {
    if (auto own = dreadovania::own_certificate_files(server.server)) {
        if (auto password = read_file(on_sd(own->password).c_str())) {
            while (!password->empty() && (password->back() == '\n' || password->back() == '\r')) {
                password->pop_back();
            }
            std::ifstream exists(on_sd(own->p12), std::ios::binary);
            if (exists) {
                return {on_sd(own->p12), *password};
            }
        }
    }
    if (!server.cert_password.empty()) {
        std::string path = on_sd(dreadovania::certificate_file(server.device_id));
        std::ifstream exists(path, std::ios::binary);
        if (exists) {
            return {path, server.cert_password};
        }
    }
    return {"", ""};
}

std::string utc_now() {
    std::time_t now = std::time(nullptr);
    std::tm utc{};
    if (gmtime_r(&now, &utc) == nullptr) {
        return "";
    }
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &utc);
    return text;
}

// The paired servers. On the first start after updating from a single-server
// version, device.json becomes the list's first entry and is then removed, so
// the token is only stored once (ADR 0010).
ServerList load_servers() {
    if (auto text = read_file(kServersFile)) {
        return dreadovania::parse_server_list(*text).value_or(ServerList{});
    }
    auto old = read_file(kDeviceFile);
    auto config = old ? dreadovania::parse_device_config(*old) : std::nullopt;
    if (!config) {
        return ServerList{};
    }
    ServerList list = dreadovania::server_list_from(*config);
    if (save_servers(list)) {
        std::remove(kDeviceFile);
    }
    return list;
}

// The last server address entered, kept even when pairing fails or after
// unpairing, so it never has to be typed twice.
constexpr const char* kServerFile = "sdmc:/config/dreadovania/server.txt";

std::string load_last_server() {
    std::ifstream in(kServerFile);
    std::string line;
    std::getline(in, line);
    auto server = dreadovania::normalize_server_url(line);
    return server.origin.value_or("");
}

void save_last_server(const std::string& origin) {
    std::ofstream out(kServerFile, std::ios::trunc);
    out << origin << "\n";
}

// ---- the Switch's own UI pieces --------------------------------------------------

std::optional<std::string> keyboard(const char* header, const char* guide,
                                    const std::string& initial, int max_length) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return std::nullopt;
    }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, header);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetStringLenMax(&kbd, static_cast<u32>(max_length));
    char text[256] = {};
    Result rc = swkbdShow(&kbd, text, sizeof(text));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) {
        return std::nullopt;  // cancelled
    }
    return std::string(text);
}

std::string console_nickname() {
    std::string name = "Switch";
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysDeviceNickName nick = {};
        if (R_SUCCEEDED(setsysGetDeviceNickname(&nick)) && nick.nickname[0] != '\0') {
            name = nick.nickname;
        }
        setsysExit();
    }
    return name;
}

// ---- the app ----------------------------------------------------------------------------

using ui::Hint;
namespace color = ui::color;

using ui::Dialog;
using ui::Screen;
using ui::kAsideW;
using ui::kRowGap;
using ui::kRowH;

std::uint64_t now_ms() { return armTicksToNs(armGetSystemTick()) / 1'000'000; }

// "dread.example.com:8443" for "https://dread.example.com:8443", for messages.
std::string bare_origin(const std::string& origin) {
    return origin.rfind("https://", 0) == 0 ? origin.substr(8) : origin;
}

class App {
  public:
    explicit App(std::string self_path) : self_path_(std::move(self_path)) {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad_);
        hidInitializeTouchScreen();
    }

    // Returns an error if the screen couldn't start.
    std::string start() {
        std::string error = gfx_.init();
        if (!error.empty()) {
            return error;
        }
        // Something on screen at once: preparing the trusted certificates can take a
        // few seconds the first time (ADRs 0013-0015).
        v_.screen = Screen::Starting;
        v_.busy = "Starting...";
        render();
        http_.set_wait_hook([this] { tick_busy(); });
        net_error_ = http_.init([this](const std::string& step) {
            v_.busy = step;
            ++v_.busy_frame;
            render();
        });
        v_.busy.clear();
        v_.applet_mode = !app::system_state().app_is_application;
        servers_ = load_servers();
        pick_server();
        v_.last_server = config_ ? config_->server : load_last_server();
        v_.screen = config_ ? Screen::Library : Screen::Pair;
        if (config_) {
            render();  // something on screen while the list loads
            refresh();
        }
        render();
        return "";
    }

    // Shows what's running, with a spinner, until it goes out of scope.
    class Busy {
      public:
        Busy(App& app, std::string text) : app_(app), previous_(app.v_.busy) {
            app_.v_.busy = std::move(text);
            app_.render();
        }
        ~Busy() {
            app_.v_.busy = previous_;
            app_.render();
        }
        Busy(const Busy&) = delete;
        Busy& operator=(const Busy&) = delete;

      private:
        App& app_;
        std::string previous_;
    };

    // While a request waits: turn the spinner, at most ten times a second.
    void tick_busy() {
        if (v_.busy.empty() && v_.screen != Screen::Working) {
            return;
        }
        std::uint64_t now = now_ms();
        if (now - last_tick_ < 100) {
            return;
        }
        last_tick_ = now;
        ++v_.busy_frame;
        render();
    }

    // One frame. Returns false to exit.
    bool step() {
        padUpdate(&pad_);
        u64 down = padGetButtonsDown(&pad_);
        if (down & HidNpadButton_Plus) {
            return false;
        }
        handle_touch();
        if (v_.dialog != Dialog::None) {
            dialog_step(down);
        } else {
            switch (v_.screen) {
                case Screen::Pair:
                    pair_step(down);
                    break;
                case Screen::Library:
                    library_step(down);
                    break;
                case Screen::Presets:
                    presets_step(down);
                    break;
                case Screen::Servers:
                    servers_step(down);
                    break;
                case Screen::Working:
                case Screen::Starting:
                    break;
            }
        }
        if (quitting_) {
            return false;
        }
        render();
        return true;
    }

    void stop() {
        http_.shutdown();
        gfx_.shutdown();
    }

  private:
    std::string self_path_;          // how the app was started (argv[0]), for updating
    dreadovania::AppLatest latest_;  // the server's answer to /app/latest
    PadState pad_{};
    ui::Gfx gfx_;
    app::Http http_;
    std::string net_error_;
    ServerList servers_;                  // every paired server (ADR 0010)
    std::optional<PairedServer> config_;  // the one in use
    std::set<std::string> renewal_checked_;  // servers whose certificate was checked
    std::string network_;                 // the network the console is on, or ""
    std::optional<std::string> picked_on_;  // the network at the last automatic choice
    bool quitting_ = false;
    bool touching_ = false;

    ui::View v_;                 // everything the screens show
    std::string overwrite_for_;  // seed the user agreed to install over other mods' files
    std::uint64_t toast_until_ = 0;
    std::uint64_t last_tick_ = 0;  // when the spinner last turned

    // ---- messages ---------------------------------------------------------------------

    void say(const std::string& text, bool bad) {
        v_.toast = text;
        v_.toast_bad = bad;
        toast_until_ = now_ms() + (bad ? 9000 : 7000) + text.size() * 20;
    }

    std::string url(const char* path) const { return config_->server + path; }

    std::string explain(const app::Response& r, const char* fallback) const {
        if (!r.error.empty()) {
            return r.error;
        }
        auto detail = dreadovania::problem_detail(r.body);
        return (detail ? *detail : std::string(fallback)) + " (HTTP " +
               std::to_string(r.status) + ")";
    }

    // ---- input --------------------------------------------------------------------------

    void move(std::size_t& cursor, std::size_t count, u64 down) {
        if (count == 0) {
            return;
        }
        if ((down & HidNpadButton_AnyDown) && cursor + 1 < count) {
            ++cursor;
        } else if ((down & HidNpadButton_AnyUp) && cursor > 0) {
            --cursor;
        }
    }

    void pair_step(u64 down) {
        if ((down & HidNpadButton_B) && v_.pairing_another) {
            open_servers();  // back without pairing
            return;
        }
        if (down & HidNpadButton_A) {
            pair(v_.last_server.empty());
        } else if ((down & HidNpadButton_Y) && !v_.last_server.empty()) {
            pair(true);
        }
    }

    void library_step(u64 down) {
        if (down & HidNpadButton_Minus) {
            if (!v_.update_version.empty()) {
                update_app();
            } else if (v_.update_required) {
                say("This app is too old for the server, and the server has no update to "
                    "offer. Download the latest app from its Releases page.", true);
            }
            return;
        }
        if (v_.update_required && (down & (HidNpadButton_A | HidNpadButton_R))) {
            say("This app is too old for the server. Press - to update it first.", true);
            return;
        }
        if (down & HidNpadButton_A) {
            get_selected();
        } else if (down & HidNpadButton_R) {
            open_presets();
        } else if (down & HidNpadButton_Y) {
            refresh();
        } else if (down & HidNpadButton_L) {
            open_servers();
        } else if (down & HidNpadButton_ZL) {
            check_for_update_now();
        } else if (down & (HidNpadButton_AnyDown | HidNpadButton_AnyUp)) {
            std::size_t before = v_.cursor;
            move(v_.cursor, v_.seeds.size(), down);
            if (v_.cursor != before) {
                overwrite_for_.clear();  // a replace confirmation is for one seed only
            }
        }
    }

    void presets_step(u64 down) {
        if (down & HidNpadButton_A) {
            new_seed();
        } else if (down & HidNpadButton_B) {
            v_.screen = Screen::Library;
        } else if (down & HidNpadButton_X) {
            v_.race = !v_.race;
        } else {
            move(v_.preset_cursor, v_.presets.size(), down);
        }
    }

    void dialog_step(u64 down) {
        if (v_.dialog == Dialog::Replace) {
            if (down & HidNpadButton_A) {
                v_.dialog = Dialog::None;
                get_selected();  // overwrite_for_ is this seed: replaces the files
            } else if (down & HidNpadButton_B) {
                v_.dialog = Dialog::None;
                overwrite_for_.clear();
                say("Nothing was changed on the SD card.", false);
            }
        } else if (v_.dialog == Dialog::Unpair) {
            if (down & HidNpadButton_A) {
                v_.dialog = Dialog::None;
                if (config_ && v_.unpair_target == config_->server) {
                    unpair();
                } else {
                    unpair_other(v_.unpair_target);
                }
            } else if (down & HidNpadButton_B) {
                v_.dialog = Dialog::None;
            }
        }
    }

    // Tap a row to select it; tap the selected row again to act on it.
    void handle_touch() {
        HidTouchScreenState state{};
        bool touched = hidGetTouchScreenStates(&state, 1) > 0 && state.count > 0;
        bool tapped = touched && !touching_;
        touching_ = touched;
        if (!tapped || v_.dialog != Dialog::None) {
            return;
        }
        const int x = static_cast<int>(state.touches[0].x);
        const int y = static_cast<int>(state.touches[0].y);
        auto hit = [&](int left, int right, std::size_t first, std::size_t count) -> long {
            if (x < left || x > right || y < ui::list_top()) {
                return -1;
            }
            std::size_t index = first + static_cast<std::size_t>((y - ui::list_top()) / (kRowH + kRowGap));
            return index < count ? static_cast<long>(index) : -1;
        };
        if (v_.screen == Screen::Library) {
            long i = hit(kAsideW + 32, ui::Gfx::kWidth - 32, ui::first_visible(v_.cursor, v_.seeds.size()),
                         v_.seeds.size());
            if (i >= 0) {
                if (static_cast<std::size_t>(i) == v_.cursor) {
                    get_selected();
                } else {
                    v_.cursor = static_cast<std::size_t>(i);
                    overwrite_for_.clear();
                }
            }
        } else if (v_.screen == Screen::Presets) {
            long i = hit(24, kAsideW - 24, ui::first_visible(v_.preset_cursor, v_.presets.size()),
                         v_.presets.size());
            if (i >= 0) {
                if (static_cast<std::size_t>(i) == v_.preset_cursor) {
                    new_seed();
                } else {
                    v_.preset_cursor = static_cast<std::size_t>(i);
                }
            }
        }
    }

    // ---- long work: generate, patch, install ---------------------------------------------

    // About a second, still drawing and answering the system and the B button.
    bool wait_a_moment() {
        for (int i = 0; i < 20; ++i) {
            if (!appletMainLoop()) {
                quitting_ = true;
                return false;
            }
            padUpdate(&pad_);
            if (padGetButtonsDown(&pad_) & HidNpadButton_B) {
                return false;
            }
            render();
            svcSleepThread(50'000'000LL);
        }
        return true;
    }

    void begin_work(const std::string& title, std::size_t first_step) {
        v_.work = ui::Work{};
        v_.work.title = title;
        v_.work.steps = {"Generate", "Patch with your game files", "Download and check", "Install"};
        v_.work.current = first_step;
        v_.screen = Screen::Working;
        render();
    }

    void step_to(std::size_t index) {
        v_.work.current = index;
        v_.work.detail.clear();
        v_.work.fraction = -1;
        render();
    }

    app::Status status() {
        return [this](const std::string& line) {
            v_.work.detail = line;
            render();
        };
    }

    // Whatever the selected seed still needs: finish generating, patch, then install.
    // Why installing now would be unsafe (Dread suspended in the background), or nothing.
    std::optional<std::string> install_blocked() {
        app::SystemState state = app::system_state();
        v_.applet_mode = !state.app_is_application;
        return dreadovania::install_blocker(state.app_is_application, state.running_program);
    }

    void get_selected() {
        if (v_.seeds.empty()) {
            say("No seeds yet. Press R to generate one.", false);
            return;
        }
        SeedSummary seed = v_.seeds[v_.cursor];
        if (seed.status == "failed" || seed.status == "cancelled") {
            say("This seed " + seed.status + " on the server. Press R for a new one.", true);
            return;
        }
        if (seed.status == "ready" && seed.mod_status == "ready") {
            if (auto blocked = install_blocked()) {
                say(*blocked, true);  // checked before anything starts
                return;
            }
        }
        std::size_t first = seed.status == "pending" ? 0 : seed.mod_status == "ready" ? 2 : 1;
        begin_work(seed.status == "ready" && seed.mod_status == "ready" ? "Installing your seed"
                                                                         : "Getting your seed",
                   first);
        v_.work.seed_hash = seed.seed_hash;
        deliver(seed);
    }

    // Generate from the chosen preset, then patch and install.
    void new_seed() {
        if (v_.presets.empty()) {
            return;
        }
        const PresetSummary& preset = v_.presets[v_.preset_cursor];
        begin_work("Getting your new seed", 0);
        app::JobResult made = app::generate_seed(http_, config_->server, config_->token,
                                                 preset.id, v_.race, status(),
                                                 [this] { return wait_a_moment(); });
        if (!made.ok) {
            finish(made.seed_id, made.message, !made.stopped);
            return;
        }
        SeedSummary seed;
        seed.id = made.seed_id;
        seed.status = "ready";
        deliver(seed);
    }

    void deliver(const SeedSummary& seed) {
        auto wait = [this] { return wait_a_moment(); };
        const std::string& server = config_->server;
        const std::string& token = config_->token;
        if (seed.status == "pending") {
            if (seed.job_id.empty()) {
                finish(seed.id, "This seed is still being generated. Press Y to refresh.", false);
                return;
            }
            app::JobResult made =
                app::follow_job(http_, server, token, seed.job_id, "Generating", status(), wait);
            if (!made.ok) {
                finish(seed.id, made.message, !made.stopped);
                return;
            }
        }
        std::string mod = seed.status == "pending" ? "" : seed.mod_status;
        if (mod != "ready") {
            step_to(1);
            app::JobResult patched =
                mod == "pending" && !seed.mod_job_id.empty()
                    ? app::follow_job(http_, server, token, seed.mod_job_id, "Patching", status(),
                                      wait)
                    : app::patch_seed(http_, server, token, seed.id, status(), wait);
            if (!patched.ok) {
                finish(seed.id, patched.message, !patched.stopped);
                return;
            }
        }
        if (auto blocked = install_blocked()) {
            finish(seed.id, *blocked + " The seed is patched: press A on it once Dread is closed.",
                   true);
            return;
        }
        step_to(2);
        app::InstallResult result = app::install_seed(
            http_, server, token, seed.id, overwrite_for_ == seed.id,
            [this](const std::string& line) {
                if (line.rfind("Installing", 0) == 0 && v_.work.current < 3) {
                    step_to(3);
                }
                v_.work.detail = line;
                render();
            },
            [this](double fraction) { v_.work.fraction = fraction; });
        overwrite_for_ = result.conflicts.empty() ? "" : seed.id;
        v_.conflicts = result.conflicts;
        finish(seed.id, result.message, !result.installed && result.conflicts.empty());
        if (!result.conflicts.empty()) {
            v_.dialog = Dialog::Replace;
            v_.toast.clear();
        }
    }

    // Back to the list, with the seed selected and the outcome shown.
    void finish(const std::string& seed_id, const std::string& message, bool bad) {
        if (quitting_) {
            return;
        }
        v_.screen = Screen::Library;
        refresh();
        for (std::size_t i = 0; i < v_.seeds.size(); ++i) {
            if (v_.seeds[i].id == seed_id) {
                v_.cursor = i;
            }
        }
        if (v_.loaded || v_.toast.empty()) {
            say(message, bad);
        }
    }

    // ---- server calls ---------------------------------------------------------------------

    void open_presets() {
        if (!net_error_.empty()) {
            say(net_error_, true);
            return;
        }
        app::Response r;
        {
            Busy busy(*this, "Loading presets...");
            r = http_.request("GET", url("/api/v1/presets"), "", config_->token);
        }
        auto presets = r.status == 200 ? dreadovania::parse_preset_list(r.body) : std::nullopt;
        if (!presets) {
            say("Couldn't load presets: " + explain(r, "unexpected answer"), true);
            return;
        }
        if (presets->empty()) {
            say("The server has no presets yet.", true);
            return;
        }
        v_.presets = std::move(*presets);
        v_.preset_cursor = std::min(v_.preset_cursor, v_.presets.size() - 1);
        v_.screen = Screen::Presets;
    }

    void pair(bool change_server) {
        if (!net_error_.empty()) {
            say(net_error_, true);
            return;
        }
        dreadovania::ServerUrl server;
        server.origin = v_.last_server;
        if (change_server || v_.last_server.empty()) {
            auto typed = keyboard("Server address", "https://dread.example.com",
                                  v_.last_server.empty() ? "https://" : v_.last_server, 200);
            if (!typed) {
                return;
            }
            server = dreadovania::normalize_server_url(*typed);
            if (!server.origin) {
                say("Not paired: " + server.error + ".", true);
                return;
            }
            v_.last_server = *server.origin;
            save_last_server(v_.last_server);  // remembered even if pairing fails
            render();
        }
        auto typed_code = keyboard("Pairing code (8 letters and numbers, no dash needed)",
                                   "From the server: Account > Switch app > Pair a Switch", "", 9);
        if (!typed_code) {
            return;
        }
        auto code = dreadovania::normalize_pairing_code(*typed_code);
        if (!code) {
            v_.typed_code.clear();
            say("Not paired: the code is 8 letters and numbers, like ABCDEFGH.", true);
            return;
        }
        v_.typed_code = *code;
        // No certificate for pairing: it may be through the server's front door, and
        // another server's certificate is no use here.
        http_.use_client_certificate("", "");
        app::Response r;
        {
            Busy busy(*this, "Pairing with " + bare_origin(*server.origin) + "...");
            r = http_.request("POST", *server.origin + "/api/v1/devices/pair",
                              dreadovania::pair_request_body(*code, console_nickname()));
        }
        auto paired = r.status == 201 ? dreadovania::parse_paired(r.body) : std::nullopt;
        if (!paired) {
            apply_certificate();
            say("Not paired: " + explain(r, "the server refused the code"), true);
            return;
        }
        PairedServer entry;
        // Pairing may have been through the front door: the rest is at the main address.
        auto main_url = dreadovania::normalize_server_url(paired->server_url);
        entry.server = main_url.origin ? *main_url.origin : *server.origin;
        entry.device_id = paired->device_id;
        entry.name = paired->name;
        entry.token = paired->token;
        if (paired->certificate) {
            if (save_certificate(paired->device_id, paired->certificate->p12)) {
                entry.cert_password = paired->certificate->password;
                entry.cert_sha256 = paired->certificate->sha256;
                entry.cert_not_after = paired->certificate->not_after;
                entry.cert_renew_after = paired->certificate->renew_after;
            }
        }
        ServerList updated = servers_;
        std::size_t index = dreadovania::remember_server(updated, std::move(entry));
        // Pairing here links this network to the new server, unless it's already
        // linked to another one (ADR 0010 decision 4).
        network_ = app::current_network();
        picked_on_ = network_;
        if (!dreadovania::linked_server(updated, network_)) {
            dreadovania::link_network(updated, index, network_);
        }
        if (!save_servers(updated)) {
            say("Paired, but the settings couldn't be saved to the SD card.", true);
            return;
        }
        servers_ = std::move(updated);
        config_ = servers_.servers[index];
        apply_certificate();
        v_.typed_code.clear();
        v_.pairing_another = false;
        v_.server_note.clear();
        v_.screen = Screen::Library;
        refresh();
        say("Paired as \"" + paired->name + "\".", false);
    }

    void refresh() {
        v_.loaded = false;
        if (!net_error_.empty()) {
            say(net_error_, true);
            return;
        }
        if (app::current_network() != picked_on_.value_or("")) {
            pick_server();  // moved to another network since the last choice
            if (!config_) {
                return;
            }
        }
        Busy busy(*this, "Loading seeds from " + bare_origin(config_->server) + "...");
        app::Response r = http_.request("GET", url("/api/v1/seeds?limit=50"), "", config_->token);
        if (r.status == 401) {
            std::string removed = config_->server;
            drop_current();
            say("This Switch was removed on " + removed + ". Pair it again.", true);
            return;
        }
        auto seeds = r.status == 200 ? dreadovania::parse_seed_list(r.body) : std::nullopt;
        if (seeds) {
            renew_certificate_if_due();
        }
        if (!seeds) {
            say("Couldn't load seeds: " + explain(r, "unexpected answer"), true);
            return;
        }
        v_.seeds = std::move(*seeds);
        v_.cursor = v_.seeds.empty() ? 0 : std::min(v_.cursor, v_.seeds.size() - 1);
        v_.loaded = true;
        v_.installed = app::installed_seed().value_or("");
        check_for_update();
    }

    // ---- updating the app (server ADR 0021) ---------------------------------------------

    // Asks the server about the app's latest release. With check_now, the server
    // asks GitHub again rather than answer from its last check (server ADR 0022).
    // Returns false if the server didn't answer.
    bool check_for_update(bool check_now = false) {
        const char* path = check_now ? "/api/v1/app/latest?check=true" : "/api/v1/app/latest";
        app::Response r = http_.request("GET", url(path), "", config_->token);
        auto latest = r.status == 200 ? dreadovania::parse_app_latest(r.body) : std::nullopt;
        if (!latest) {
            return false;  // an older server without updates, or a hiccup: try again next refresh
        }
        latest_ = *latest;
        auto need = dreadovania::update_need(dreadovania::kVersion, latest_);
        v_.update_required = need == dreadovania::UpdateNeed::Required;
        bool newer = latest_.available &&
                     dreadovania::compare_versions(latest_.version, dreadovania::kVersion) > 0;
        v_.update_version = newer ? latest_.version : "";
        return true;
    }

    // The "Check for updates" button: ask now, and say what was found.
    void check_for_update_now() {
        bool found;
        {
            Busy busy(*this, "Checking for updates...");
            found = check_for_update(true);
        }
        if (!found) {
            say("The server didn't say whether there's an update. It may be too old to "
                "offer app updates.", true);
        } else if (!v_.update_version.empty()) {
            say("Version " + v_.update_version + " is ready: press - to update.", false);
        } else if (latest_.available) {
            say("This app is up to date (" + std::string(dreadovania::kVersion) + ").", false);
        } else {
            say("No update offered: " + latest_.reason + ".", false);
        }
    }

    void update_app() {
        begin_work("Updating the app to " + latest_.version, 0);
        v_.work.steps = {"Download and check", "Replace the app", "Restart"};
        render();
        app::UpdateResult result = app::download_update(
            http_, config_->server, config_->token, latest_, self_path_, [this](double f) {
                v_.work.fraction = f;
                render();
            });
        if (!result.ok) {
            v_.screen = Screen::Library;
            say(result.message, true);
            return;
        }
        step_to(2);
        // Ask the Homebrew Menu to start the new version when this one exits.
        if (R_SUCCEEDED(envSetNextLoad(self_path_.c_str(), self_path_.c_str()))) {
            quitting_ = true;
            return;
        }
        v_.screen = Screen::Library;
        v_.update_version.clear();
        v_.update_required = false;
        say(result.message + " Close the app and open it again to use it.", false);
    }

    void unpair() {
        // Tell the server first; forget the token here whatever it answers.
        {
            Busy busy(*this, "Unpairing...");
            http_.request("DELETE", url("/api/v1/devices/me"), "", config_->token);
        }
        std::string removed = config_->server;
        if (drop_current()) {
            say("Unpaired from " + removed + ".", false);
        } else {
            say("Unpaired. Press A to pair again.", false);
        }
    }

    // ---- client certificates (server ADR 0023) -------------------------------------------

    // Present the certificate for the server in use, or none.
    void apply_certificate() {
        if (!config_) {
            http_.use_client_certificate("", "");
            return;
        }
        auto [path, password] = certificate_for(*config_);
        http_.use_client_certificate(path, password);
    }

    // Once per server each session: if the server says it's time (a third of the
    // certificate's life left, or it now gets them from another CA), fetch the
    // replacement. The old one keeps working until the server sees the new one.
    void renew_certificate_if_due() {
        if (!config_ || renewal_checked_.count(config_->server) != 0) {
            return;
        }
        renewal_checked_.insert(config_->server);
        Busy busy(*this, "Checking this Switch's certificate...");
        app::Response me = http_.request("GET", url("/api/v1/devices/me"), "", config_->token);
        if (me.status != 200) {
            return;
        }
        auto status = dreadovania::parse_certificate_status(me.body);
        if (status && status->issuer == "external") {
            return;  // from the owner's own CA: renewed there, not by the server
        }
        bool first = !status && config_->cert_password.empty() &&
                     me.body.find("\"certificate\"") != std::string::npos;  // a newer server
        if (!first && !(status && dreadovania::renewal_due(status->renew_after, utc_now()))) {
            return;
        }
        app::Response r =
            http_.request("POST", url("/api/v1/devices/me/certificate"), "", config_->token);
        auto cert = r.status == 200 ? dreadovania::parse_client_certificate(r.body) : std::nullopt;
        if (!cert || !save_certificate(config_->device_id, cert->p12)) {
            return;  // try again next time: the current certificate still works
        }
        for (PairedServer& s : servers_.servers) {
            if (s.server == config_->server) {
                s.cert_password = cert->password;
                s.cert_sha256 = cert->sha256;
                s.cert_not_after = cert->not_after;
                s.cert_renew_after = cert->renew_after;
                config_ = s;
            }
        }
        save_servers(servers_);
        apply_certificate();  // the next request uses the new one, which retires the old
    }

    // ---- the Servers screen (ADR 0010) -------------------------------------------------

    void open_servers() {
        network_ = app::current_network();
        sync_servers_view();
        v_.server_cursor = 0;
        for (std::size_t i = 0; i < v_.servers.size(); ++i) {
            if (v_.servers[i].in_use) {
                v_.server_cursor = i;
            }
        }
        v_.pairing_another = false;
        v_.screen = Screen::Servers;
    }

    void sync_servers_view() {
        v_.servers.clear();
        for (const PairedServer& s : servers_.servers) {
            v_.servers.push_back({s.server, s.name, s.networks, config_ && config_->server == s.server});
        }
        v_.network = network_;
        if (!v_.servers.empty()) {
            v_.server_cursor = std::min(v_.server_cursor, v_.servers.size() - 1);
        }
    }

    void servers_step(u64 down) {
        if (down & HidNpadButton_B) {
            v_.screen = config_ ? Screen::Library : Screen::Pair;
        } else if (down & HidNpadButton_Y) {
            v_.pairing_another = true;
            v_.last_server.clear();  // a new address
            v_.typed_code.clear();
            v_.screen = Screen::Pair;
        } else if (servers_.servers.empty()) {
            return;
        } else if (down & HidNpadButton_A) {
            use_server(v_.server_cursor);
        } else if (down & HidNpadButton_X) {
            toggle_link(v_.server_cursor);
        } else if (down & HidNpadButton_R) {
            v_.unpair_target = servers_.servers[v_.server_cursor].server;
            v_.dialog = Dialog::Unpair;
        } else {
            move(v_.server_cursor, servers_.servers.size(), down);
        }
    }

    // Switch to a server by hand. It stays in use until the network changes.
    void use_server(std::size_t index) {
        const PairedServer& chosen = servers_.servers[index];
        bool changed = !config_ || config_->server != chosen.server;
        dreadovania::mark_used(servers_, index);
        save_servers(servers_);
        config_ = servers_.servers[index];
        apply_certificate();
        picked_on_ = network_;
        bool here = dreadovania::linked_server(servers_, network_) == index;
        v_.server_note = here || servers_.servers.size() < 2 || network_.empty()
                             ? ""
                             : "Not linked to " + network_;
        v_.screen = Screen::Library;
        if (changed) {
            v_.seeds.clear();
            refresh();
            say("Using " + config_->server + ".", false);
        }
    }

    void toggle_link(std::size_t index) {
        if (network_.empty()) {
            say("The Switch isn't connected to a network it can name.", true);
            return;
        }
        bool linked = dreadovania::linked_server(servers_, network_) == index;
        if (linked) {
            dreadovania::unlink_network(servers_, index, network_);
        } else {
            dreadovania::link_network(servers_, index, network_);
        }
        if (!save_servers(servers_)) {
            say("The settings couldn't be saved to the SD card.", true);
            return;
        }
        if (config_ && config_->server == servers_.servers[index].server) {
            config_->networks = servers_.servers[index].networks;
            v_.server_note.clear();
        }
        sync_servers_view();
        say(linked ? network_ + " is no longer linked to a server."
                   : network_ + " now uses " + servers_.servers[index].server + ".",
            false);
    }

    // Unpair a server that isn't the one in use: tell it, then forget its token.
    void unpair_other(const std::string& origin) {
        for (const PairedServer& s : servers_.servers) {
            if (s.server == origin) {
                auto [path, password] = certificate_for(s);  // its own, just for this
                http_.use_client_certificate(path, password);
                Busy busy(*this, "Unpairing from " + bare_origin(s.server) + "...");
                http_.request("DELETE", s.server + "/api/v1/devices/me", "", s.token);
                remove_certificate(s.device_id);
            }
        }
        apply_certificate();
        dreadovania::forget_server(servers_, origin);
        save_servers(servers_);
        sync_servers_view();
        say("Unpaired from " + origin + ".", false);
    }

    // Forget the server in use (unpaired, or removed on the server). Another
    // paired server, if any, takes its place. Returns whether there was one.
    bool drop_current() {
        v_.last_server = config_->server;
        save_last_server(v_.last_server);
        remove_certificate(config_->device_id);
        dreadovania::forget_server(servers_, config_->server);
        save_servers(servers_);
        config_.reset();
        apply_certificate();
        v_.seeds.clear();
        v_.loaded = false;
        pick_server();
        if (config_) {
            v_.screen = Screen::Library;
            refresh();
            return true;
        }
        v_.screen = Screen::Pair;
        return false;
    }

    // Choose the server for the network the console is on (ADR 0010 decision 3).
    void pick_server() {
        network_ = app::current_network();
        picked_on_ = network_;
        dreadovania::ServerChoice choice = dreadovania::choose_server(servers_, network_);
        v_.server_note.clear();
        if (!choice.index) {
            config_.reset();
            apply_certificate();
            return;
        }
        std::size_t index = *choice.index;
        if (dreadovania::most_recent(servers_) != index) {
            dreadovania::mark_used(servers_, index);
            save_servers(servers_);
        }
        bool changed = !config_ || config_->server != servers_.servers[index].server;
        config_ = servers_.servers[index];
        apply_certificate();
        if (changed) {
            v_.seeds.clear();
            v_.loaded = false;
        }
        if (servers_.servers.size() > 1 && choice.reason != dreadovania::ChoiceReason::Network) {
            v_.server_note = network_.empty() ? "Network unknown: using the last server"
                                              : "Not linked to " + network_;
        }
        if (changed && servers_.servers.size() > 1) {
            say("Using " + config_->server +
                    (v_.server_note.empty() ? " for " + network_ : ": " + v_.server_note) + ".",
                false);
        }
    }

    // ---- drawing ------------------------------------------------------------------------

    void render() {
        v_.server = config_ ? config_->server : "";
        v_.connected = config_.has_value() && v_.loaded;
        v_.user_ca = http_.user_ca();
        v_.show_toast = now_ms() < toast_until_;
        ui::draw(gfx_, v_);
        gfx_.present();
    }
};

}  // namespace

int main(int argc, char** argv) {
    App app(argc > 0 && argv[0] != nullptr ? argv[0] : "");
    std::string error = app.start();
    if (!error.empty()) {
        // No screen to draw on: fall back to libnx's console for the reason.
        consoleInit(nullptr);
        printf("Dreadovania couldn't start: %s\n\nPress + to exit.\n", error.c_str());
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
                break;
            }
            consoleUpdate(nullptr);
        }
        consoleExit(nullptr);
        app.stop();
        return 1;
    }
    while (appletMainLoop()) {
        if (!app.step()) {
            break;
        }
    }
    app.stop();
    return 0;
}
