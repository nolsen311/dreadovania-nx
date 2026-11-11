// SPDX-License-Identifier: GPL-3.0-or-later
// Dreadovania for Switch (ADR 0004): pair with a Dreadovania server, list its
// seeds, generate and patch new ones there, and install them. Drawn with SDL2
// (ADR 0009, docs/design).
#include <switch.h>

#include <cstdio>
#include <fstream>
#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "dreadovania/api.hpp"
#include "dreadovania/version.hpp"
#include "http.hpp"
#include "installer.hpp"
#include "jobs.hpp"
#include "system.hpp"
#include "ui/gfx.hpp"
#include "ui/screens.hpp"
#include "ui/widgets.hpp"

namespace {

using dreadovania::DeviceConfig;
using dreadovania::PresetSummary;
using dreadovania::SeedSummary;

constexpr const char* kDeviceFile = "sdmc:/config/dreadovania/device.json";

// ---- settings on the SD card -------------------------------------------------

std::optional<DeviceConfig> load_config() {
    std::ifstream in(kDeviceFile, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return dreadovania::parse_device_config(ss.str());
}

bool save_config(const DeviceConfig& config) {
    const std::string tmp = std::string(kDeviceFile) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << dreadovania::serialize_device_config(config);
        if (!out) {
            return false;
        }
    }
    std::remove(kDeviceFile);
    return std::rename(tmp.c_str(), kDeviceFile) == 0;
}

void forget_config() { std::remove(kDeviceFile); }

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

class App {
  public:
    App() {
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
        net_error_ = http_.init();
        v_.applet_mode = !app::system_state().app_is_application;
        config_ = load_config();
        v_.last_server = config_ ? config_->server : load_last_server();
        v_.screen = config_ ? Screen::Library : Screen::Pair;
        if (config_) {
            render();  // something on screen while the list loads
            refresh();
        }
        return "";
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
                case Screen::Working:
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
    PadState pad_{};
    ui::Gfx gfx_;
    app::Http http_;
    std::string net_error_;
    std::optional<DeviceConfig> config_;
    bool quitting_ = false;
    bool touching_ = false;

    ui::View v_;                 // everything the screens show
    std::string overwrite_for_;  // seed the user agreed to install over other mods' files
    std::uint64_t toast_until_ = 0;

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
        if (down & HidNpadButton_A) {
            pair(v_.last_server.empty());
        } else if ((down & HidNpadButton_Y) && !v_.last_server.empty()) {
            pair(true);
        }
    }

    void library_step(u64 down) {
        if (down & HidNpadButton_A) {
            get_selected();
        } else if (down & HidNpadButton_R) {
            open_presets();
        } else if (down & HidNpadButton_Y) {
            refresh();
        } else if (down & HidNpadButton_X) {
            v_.dialog = Dialog::Unpair;
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
                unpair();
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
        app::Response r = http_.request("GET", url("/api/v1/presets"), "", config_->token);
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
        say("Pairing...", false);
        render();
        app::Response r = http_.request("POST", *server.origin + "/api/v1/devices/pair",
                                        dreadovania::pair_request_body(*code, console_nickname()));
        auto paired = r.status == 201 ? dreadovania::parse_paired(r.body) : std::nullopt;
        if (!paired) {
            say("Not paired: " + explain(r, "the server refused the code"), true);
            return;
        }
        DeviceConfig config{*server.origin, paired->device_id, paired->name, paired->token};
        if (!save_config(config)) {
            say("Paired, but the settings couldn't be saved to the SD card.", true);
            return;
        }
        config_ = config;
        v_.typed_code.clear();
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
        app::Response r = http_.request("GET", url("/api/v1/seeds?limit=50"), "", config_->token);
        if (r.status == 401) {
            v_.last_server = config_->server;
            save_last_server(v_.last_server);
            forget_config();
            config_.reset();
            v_.screen = Screen::Pair;
            say("This Switch was removed on the server. Pair it again.", true);
            return;
        }
        auto seeds = r.status == 200 ? dreadovania::parse_seed_list(r.body) : std::nullopt;
        if (!seeds) {
            say("Couldn't load seeds: " + explain(r, "unexpected answer"), true);
            return;
        }
        v_.seeds = std::move(*seeds);
        v_.cursor = v_.seeds.empty() ? 0 : std::min(v_.cursor, v_.seeds.size() - 1);
        v_.loaded = true;
        v_.installed = app::installed_seed().value_or("");
    }

    void unpair() {
        // Tell the server first; forget the token here whatever it answers.
        http_.request("DELETE", url("/api/v1/devices/me"), "", config_->token);
        v_.last_server = config_->server;
        save_last_server(v_.last_server);
        forget_config();
        config_.reset();
        v_.seeds.clear();
        v_.screen = Screen::Pair;
        say("Unpaired. Press A to pair again.", false);
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

int main(int /*argc*/, char** /*argv*/) {
    App app;
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
