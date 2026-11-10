// SPDX-License-Identifier: GPL-3.0-or-later
// Dreadovania for Switch (ADR 0004): pair with a Dreadovania server, list its
// seeds, generate and patch new ones there, and install them. A text UI for now.
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

namespace {

using dreadovania::DeviceConfig;
using dreadovania::PresetSummary;
using dreadovania::SeedSummary;

constexpr const char* kDeviceFile = "sdmc:/config/dreadovania/device.json";
constexpr std::size_t kWidth = 78;  // the console is 80 columns
constexpr std::size_t kVisibleSeeds = 30;

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

// ---- screens ---------------------------------------------------------------------------

class App {
  public:
    App() {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad_);
    }

    void start() {
        net_error_ = http_.init();
        config_ = load_config();
        last_server_ = config_ ? config_->server : load_last_server();
        if (config_) {
            refresh();
        }
        draw();
    }

    // Returns false to exit.
    bool step() {
        padUpdate(&pad_);
        u64 down = padGetButtonsDown(&pad_);
        if (down & HidNpadButton_Plus) {
            return false;
        }
        if (config_ && screen_ == Screen::Presets) {
            presets_step(down);
            return !quitting_;
        }
        if (!config_) {
            if (down & HidNpadButton_A) {
                pair(/*change_server=*/last_server_.empty());
                draw();
            } else if ((down & HidNpadButton_Y) && !last_server_.empty()) {
                pair(/*change_server=*/true);
                draw();
            }
        } else if (down & HidNpadButton_A) {
            get_selected();
            draw();
        } else if (down & HidNpadButton_R) {
            open_presets();
            draw();
        } else if (down & HidNpadButton_Y) {
            refresh();
            draw();
        } else if (down & HidNpadButton_X) {
            if (confirm_unpair_) {
                unpair();
            } else {
                confirm_unpair_ = true;
                message_ = "Press X again to unpair this Switch, or any other button to keep it.";
            }
            draw();
        } else if (down & (HidNpadButton_AnyDown | HidNpadButton_AnyUp)) {
            move(down & HidNpadButton_AnyDown ? 1 : -1);
            overwrite_for_.clear();  // a replace confirmation is for one seed only
            draw();
        } else if (down != 0 && confirm_unpair_) {
            confirm_unpair_ = false;
            message_.clear();
            draw();
        }
        return !quitting_;
    }

    void stop() { http_.shutdown(); }

  private:
    PadState pad_{};
    app::Http http_;
    std::string net_error_;
    std::optional<DeviceConfig> config_;
    std::vector<SeedSummary> seeds_;
    std::size_t cursor_ = 0;
    std::string message_;
    bool loaded_ = false;
    bool confirm_unpair_ = false;
    std::string installed_;      // the seed the SD card says is installed
    std::string overwrite_for_;  // seed the user agreed to install over other mods' files
    bool quitting_ = false;      // the system asked the app to close while it was waiting

    enum class Screen { Seeds, Presets };
    Screen screen_ = Screen::Seeds;
    std::vector<PresetSummary> presets_;
    std::size_t preset_cursor_ = 0;
    bool race_ = false;

    // ---- getting a seed onto the Switch -----------------------------------------------

    static void show_status(const std::string& line) {
        printf("\r%-78.78s", line.c_str());
        consoleUpdate(nullptr);
    }

    // About a second, still answering the system and the B button.
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
            consoleUpdate(nullptr);
            svcSleepThread(50'000'000LL);
        }
        return true;
    }

    void begin_work(const std::string& title) {
        consoleClear();
        printf("%s\n\n", title.c_str());
        printf("Generating and patching take a minute or two on the server.\n");
        printf("B: stop waiting (the server carries on)\n\n");
        consoleUpdate(nullptr);
    }

    // Whatever the selected seed still needs: finish generating, patch, then install.
    void get_selected() {
        message_.clear();
        if (seeds_.empty()) {
            message_ = "No seeds yet. Press R to generate one.";
            return;
        }
        SeedSummary seed = seeds_[cursor_];
        if (seed.status == "failed" || seed.status == "cancelled") {
            message_ = "This seed " + seed.status + " on the server. Press R for a new one.";
            return;
        }
        std::string verb = seed.status == "ready" && seed.mod_status == "ready" ? "Installing"
                                                                                : "Getting";
        begin_work(verb + " " + dreadovania::describe_seed(seed, kWidth - verb.size() - 1));
        deliver(seed);
    }

    // Generate from the chosen preset, then patch and install.
    void new_seed() {
        const PresetSummary& preset = presets_[preset_cursor_];
        begin_work("New seed: " + preset.name + (race_ ? " (race)" : ""));
        app::JobResult made = app::generate_seed(
            http_, config_->server, config_->token, preset.id, race_, show_status,
            [this] { return wait_a_moment(); });
        screen_ = Screen::Seeds;
        if (!made.ok) {
            finish(made.seed_id, made.message);
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
                finish(seed.id, "This seed is still being generated. Press Y to refresh.");
                return;
            }
            app::JobResult made =
                app::follow_job(http_, server, token, seed.job_id, "Generating", show_status, wait);
            if (!made.ok) {
                finish(seed.id, made.message);
                return;
            }
        }
        std::string mod = seed.status == "pending" ? "" : seed.mod_status;
        if (mod != "ready") {
            app::JobResult patched =
                mod == "pending" && !seed.mod_job_id.empty()
                    ? app::follow_job(http_, server, token, seed.mod_job_id, "Patching",
                                      show_status, wait)
                    : app::patch_seed(http_, server, token, seed.id, show_status, wait);
            if (!patched.ok) {
                finish(seed.id, patched.message);
                return;
            }
        }
        show_status("Patched. Downloading...");
        printf("\n\nKeep the app open until it finishes.\n\n");
        app::InstallResult result = app::install_seed(
            http_, server, token, seed.id, overwrite_for_ == seed.id, show_status);
        overwrite_for_ = result.conflicts.empty() ? "" : seed.id;
        finish(seed.id, result.message);
    }

    // Back to the list, with the seed selected and the outcome shown.
    void finish(const std::string& seed_id, const std::string& message) {
        if (quitting_) {
            return;
        }
        refresh();
        for (std::size_t i = 0; i < seeds_.size(); ++i) {
            if (seeds_[i].id == seed_id) {
                cursor_ = i;
            }
        }
        std::string list_problem = loaded_ ? "" : message_;  // refresh() explains a failure
        message_ = list_problem.empty() ? message : message + "\n" + list_problem;
    }

    // ---- choosing a preset ------------------------------------------------------------

    void open_presets() {
        message_.clear();
        if (!net_error_.empty()) {
            message_ = net_error_;
            return;
        }
        app::Response r = http_.request("GET", url("/api/v1/presets"), "", config_->token);
        auto presets = r.status == 200 ? dreadovania::parse_preset_list(r.body) : std::nullopt;
        if (!presets) {
            message_ = "Couldn't load presets: " + explain(r, "unexpected answer");
            return;
        }
        if (presets->empty()) {
            message_ = "The server has no presets yet.";
            return;
        }
        presets_ = std::move(*presets);
        preset_cursor_ = std::min(preset_cursor_, presets_.size() - 1);
        screen_ = Screen::Presets;
    }

    void presets_step(u64 down) {
        if (down & HidNpadButton_A) {
            new_seed();
        } else if (down & HidNpadButton_B) {
            screen_ = Screen::Seeds;
        } else if (down & HidNpadButton_X) {
            race_ = !race_;
        } else if (down & HidNpadButton_AnyDown) {
            preset_cursor_ = std::min(preset_cursor_ + 1, presets_.size() - 1);
        } else if (down & HidNpadButton_AnyUp) {
            preset_cursor_ = preset_cursor_ > 0 ? preset_cursor_ - 1 : 0;
        } else {
            return;
        }
        if (!quitting_) {
            draw();
        }
    }

    void draw_presets() const {
        printf("New seed: choose a preset\n\n");
        std::size_t first =
            preset_cursor_ >= kVisibleSeeds ? preset_cursor_ - kVisibleSeeds + 1 : 0;
        for (std::size_t i = first; i < presets_.size() && i < first + kVisibleSeeds; ++i) {
            printf("%s %.*s\n", i == preset_cursor_ ? ">" : " ", static_cast<int>(kWidth - 2),
                   presets_[i].name.c_str());
        }
        const std::string& about = presets_[preset_cursor_].description;
        if (!about.empty()) {
            printf("\n%.*s\n", static_cast<int>(kWidth * 3), about.c_str());
        }
        printf("\nRace seed (no spoiler on the server): %s\n", race_ ? "on" : "off");
        printf("\nA: generate, patch and install    X: race on/off    B: back\n");
    }
    std::string last_server_;

    std::string url(const char* path) const { return config_->server + path; }

    std::string explain(const app::Response& r, const char* fallback) const {
        if (!r.error.empty()) {
            return r.error;
        }
        auto detail = dreadovania::problem_detail(r.body);
        return (detail ? *detail : std::string(fallback)) + " (HTTP " +
               std::to_string(r.status) + ")";
    }

    void pair(bool change_server) {
        message_.clear();
        if (!net_error_.empty()) {
            message_ = net_error_;
            return;
        }
        dreadovania::ServerUrl server;
        server.origin = last_server_;
        if (change_server || last_server_.empty()) {
            auto typed = keyboard("Server address", "https://dread.example.com",
                                  last_server_.empty() ? "https://" : last_server_, 200);
            if (!typed) {
                return;
            }
            server = dreadovania::normalize_server_url(*typed);
            if (!server.origin) {
                message_ = "Not paired: " + server.error + ".";
                return;
            }
            last_server_ = *server.origin;
            save_last_server(last_server_);  // remembered even if pairing fails
        }
        // 8 letters and numbers; the dash shown on the server is optional.
        auto typed_code = keyboard("Pairing code (8 letters and numbers, no dash needed)",
                                   "From the server: Account > Switch app > Pair a Switch", "", 9);
        if (!typed_code) {
            return;
        }
        auto code = dreadovania::normalize_pairing_code(*typed_code);
        if (!code) {
            message_ = "Not paired: the code is 8 letters and numbers, like ABCDEFGH.";
            return;
        }
        std::string name = console_nickname();
        printf("\nPairing with %s ...\n", server.origin->c_str());
        consoleUpdate(nullptr);
        app::Response r = http_.request("POST", *server.origin + "/api/v1/devices/pair",
                                        dreadovania::pair_request_body(*code, name));
        auto paired = r.status == 201 ? dreadovania::parse_paired(r.body) : std::nullopt;
        if (!paired) {
            message_ = "Not paired: " + explain(r, "the server refused the code");
            return;
        }
        DeviceConfig config{*server.origin, paired->device_id, paired->name, paired->token};
        if (!save_config(config)) {
            message_ = "Paired, but the settings couldn't be saved to the SD card.";
            return;
        }
        config_ = config;
        message_ = "Paired as \"" + paired->name + "\".";
        refresh();
    }

    void refresh() {
        loaded_ = false;
        if (!net_error_.empty()) {
            message_ = net_error_;
            return;
        }
        app::Response r = http_.request("GET", url("/api/v1/seeds?limit=50"), "", config_->token);
        if (r.status == 401) {
            last_server_ = config_->server;
            save_last_server(last_server_);
            forget_config();
            config_.reset();
            message_ = "This Switch was removed on the server. Pair it again.";
            return;
        }
        auto seeds = r.status == 200 ? dreadovania::parse_seed_list(r.body) : std::nullopt;
        if (!seeds) {
            message_ = "Couldn't load seeds: " + explain(r, "unexpected answer");
            return;
        }
        seeds_ = std::move(*seeds);
        cursor_ = seeds_.empty() ? 0 : std::min(cursor_, seeds_.size() - 1);
        loaded_ = true;
        installed_ = app::installed_seed().value_or("");
    }

    void unpair() {
        confirm_unpair_ = false;
        // Tell the server first; forget the token here whatever it answers.
        http_.request("DELETE", url("/api/v1/devices/me"), "", config_->token);
        last_server_ = config_->server;
        save_last_server(last_server_);
        forget_config();
        config_.reset();
        seeds_.clear();
        message_ = "Unpaired. Press A to pair again.";
    }

    void move(int delta) {
        if (seeds_.empty()) {
            return;
        }
        if (delta > 0 && cursor_ + 1 < seeds_.size()) {
            ++cursor_;
        } else if (delta < 0 && cursor_ > 0) {
            --cursor_;
        }
    }

    void draw() const {
        consoleClear();
        std::string version(dreadovania::kVersion);
        printf("Dreadovania %s\n", version.c_str());
        printf("%s\n\n", std::string(kWidth, '-').c_str());
        if (config_ && screen_ == Screen::Presets) {
            draw_presets();
            return;
        }
        if (!config_) {
            printf("Not paired with a server yet.\n\n");
            if (!last_server_.empty()) {
                printf("Server: %s\n\n", last_server_.c_str());
            }
            printf("1. On your server, open Account > Switch app > Pair a Switch.\n");
            printf("2. Press A here, then enter the code%s.\n\n",
                   last_server_.empty() ? " (and the server's address first)" : "");
            printf("The server must use HTTPS (see its docs/https.md).\n");
            switch (http_.user_ca()) {
                case dreadovania::UserCa::Loaded:
                    printf("Your own CA (/config/dreadovania/ca.pem) is trusted too.\n");
                    break;
                case dreadovania::UserCa::NotPem:
                    printf("/config/dreadovania/ca.pem isn't a PEM certificate: ignored.\n");
                    break;
                case dreadovania::UserCa::Missing:
                    break;
            }
        } else {
            printf("Server: %s\n\n", config_->server.c_str());
            if (loaded_ && seeds_.empty()) {
                printf("No seeds yet. Press R to generate one.\n");
            }
            std::size_t first = cursor_ >= kVisibleSeeds ? cursor_ - kVisibleSeeds + 1 : 0;
            for (std::size_t i = first; i < seeds_.size() && i < first + kVisibleSeeds; ++i) {
                std::string line = dreadovania::describe_seed(seeds_[i], kWidth - 4);
                printf("%s %s %s\n", i == cursor_ ? ">" : " ",
                       seeds_[i].id == installed_ ? "*" : " ", line.c_str());
            }
        }
        if (!message_.empty()) {
            printf("\n%s\n", message_.c_str());
        }
        printf("\n%s\n", std::string(kWidth, '-').c_str());
        if (!config_) {
            printf(last_server_.empty() ? "A: pair    +: exit\n"
                                        : "A: pair    Y: change server    +: exit\n");
        } else {
            printf("A: install    R: new seed    Up/Down: choose    Y: refresh    X: unpair\n");
            printf("+: exit    * = installed on this Switch\n");
        }
    }
};

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    consoleInit(nullptr);
    App app;
    app.start();
    while (appletMainLoop()) {
        if (!app.step()) {
            break;
        }
        consoleUpdate(nullptr);
    }
    app.stop();
    consoleExit(nullptr);
    return 0;
}
