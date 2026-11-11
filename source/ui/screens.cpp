// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens.hpp"

#include <algorithm>

#include "ui/widgets.hpp"

namespace ui {

namespace {

const int kListTop = kHeader + 24 + 36;  // under the section title

}  // namespace

int list_top() { return kListTop; }

std::size_t visible_rows() {
    return static_cast<std::size_t>((ui::Gfx::kHeight - ui::kFooter - kListTop - 12) /
                                    (kRowH + kRowGap));
}

std::size_t first_visible(std::size_t cursor, std::size_t count) {
    std::size_t rows = visible_rows();
    if (count <= rows || cursor < rows / 2) {
        return 0;
    }
    return std::min(cursor - rows / 2, count - rows);
}

static void draw_aside_installed(Gfx& g, const View& v) {
    const int x = 24;
    int y = ui::kHeader + 24;
    ui::section_title(g, x, y, "On this Switch");
    y += 36;
    const int w = kAsideW - 48;
    auto it = std::find_if(v.seeds.begin(), v.seeds.end(),
                           [&](const dreadovania::SeedSummary& s) { return s.id == v.installed; });
    if (v.installed.empty() || it == v.seeds.end()) {
        g.framed(x, y, w, 96, 14, 1, color::line, color::panel);
        g.text(v.installed.empty() ? "No seed installed" : "A seed not on this server",
                  x + 18, y + 18, ui::kSmall, color::muted, false, ui::Align::Left, w - 36);
        g.text("Choose one and press A.", x + 18, y + 50, ui::kSmall, color::fg, false,
                  ui::Align::Left, w - 36);
        y += 96 + 16;
    } else {
        std::vector<std::string> hash = g.wrap(it->seed_hash, ui::kBody, w - 36, 2);
        int h = 18 + 24 + 34 + static_cast<int>(hash.size()) * g.line_height(ui::kBody) +
                26 + 18;
        g.framed(x, y, w, h, 14, 1, color::line, color::panel);
        int ty = y + 18;
        g.text("Installed", x + 18, ty, ui::kSmall, color::muted);
        ty += 24;
        g.text(it->label, x + 18, ty, 26, color::fg, true, ui::Align::Left, w - 36);
        ty += 34;
        for (const std::string& line : hash) {
            g.text(line, x + 18, ty, ui::kBody, color::fg);
            ty += g.line_height(ui::kBody);
        }
        g.text("from " + dreadovania::short_date(it->created_at), x + 18, ty, ui::kSmall,
                  color::muted);
        y += h + 16;
    }
    std::vector<std::string> tip =
        g.wrap("Start Dread with a new save file after switching seeds.", ui::kSmall,
                  w - 36, 3);
    int h = static_cast<int>(tip.size()) * g.line_height(ui::kSmall) + 36;
    g.framed(x, y, w, h, 14, 1, color::line, color::panel);
    int ty = y + 18;
    for (const std::string& line : tip) {
        g.text(line, x + 18, ty, ui::kSmall, color::muted);
        ty += g.line_height(ui::kSmall);
    }
    if (v.applet_mode) {
        y += h + 16;
        std::vector<std::string> note = g.wrap(
            "Opened from the Album, with less memory. For the best experience, hold R while "
            "starting a game to open the Homebrew Menu.",
            ui::kSmall, w - 36, 5);
        int nh = static_cast<int>(note.size()) * g.line_height(ui::kSmall) + 36;
        g.framed(x, y, w, nh, 14, 1, color::wait, color::panel);
        ty = y + 18;
        for (const std::string& line : note) {
            g.text(line, x + 18, ty, ui::kSmall, color::fg);
            ty += g.line_height(ui::kSmall);
        }
    }
}

static void draw_library(Gfx& g, const View& v) {
    draw_aside_installed(g, v);
    g.box(kAsideW, ui::kHeader, 1, ui::Gfx::kHeight - ui::kHeader - ui::kFooter, 0,
             color::line);
    const int x = kAsideW + 32;
    const int w = ui::Gfx::kWidth - x - 32;
    ui::section_title(g, x, ui::kHeader + 24, "Your seeds");
    if (v.loaded && v.seeds.empty()) {
        g.text("No seeds yet. Press R to generate one.", x, kListTop + 8, ui::kBody,
                  color::muted);
    }
    std::size_t first = first_visible(v.cursor, v.seeds.size());
    for (std::size_t i = first; i < v.seeds.size() && i < first + visible_rows(); ++i) {
        const dreadovania::SeedSummary& s = v.seeds[i];
        int y = kListTop + static_cast<int>(i - first) * (kRowH + kRowGap);
        ui::row_box(g, x, y, w, kRowH, i == v.cursor);
        dreadovania::SeedState state = dreadovania::seed_state(s, s.id == v.installed);
        int chip_w = ui::chip(g, x + w - 20, y + kRowH / 2, state.label,
                              ui::tone_color(state.tone));
        int text_w = w - 40 - chip_w - 16;
        g.text(s.label, x + 20, y + 12, ui::kBody, color::fg, true, ui::Align::Left, text_w);
        std::string meta = dreadovania::short_date(s.created_at);
        if (!s.seed_hash.empty()) {
            meta += (meta.empty() ? "" : "  ·  ") + s.seed_hash;
        }
        g.text(meta, x + 20, y + 44, ui::kSmall, color::muted, false, ui::Align::Left, text_w);
    }
    std::string count = !v.loaded ? "Loading..." : std::to_string(v.seeds.size()) + " seeds";
    ui::footer(g, count,
               {{"A", "Install"}, {"R", "New seed"}, {"Y", "Refresh"}, {"X", "Unpair"},
                {"+", "Exit"}});
}

static void draw_presets(Gfx& g, const View& v) {
    const int x = 24;
    ui::section_title(g, x, ui::kHeader + 24, "New seed");
    std::size_t first = first_visible(v.preset_cursor, v.presets.size());
    for (std::size_t i = first; i < v.presets.size() && i < first + visible_rows(); ++i) {
        int y = kListTop + static_cast<int>(i - first) * (kRowH + kRowGap);
        ui::row_box(g, x, y, kAsideW - 48, kRowH, i == v.preset_cursor);
        g.text(v.presets[i].name, x + 20, y + 14, ui::kBody, color::fg, true,
                  ui::Align::Left, kAsideW - 88);
        g.text(v.presets[i].id.rfind("user:", 0) == 0 ? "yours" : "built in", x + 20,
                  y + 44, ui::kSmall, color::muted);
    }
    g.box(kAsideW, ui::kHeader, 1, ui::Gfx::kHeight - ui::kHeader - ui::kFooter, 0,
             color::line);
    const int rx = kAsideW + 32;
    const int rw = ui::Gfx::kWidth - rx - 32;
    if (v.presets.empty()) {
        return;
    }
    const dreadovania::PresetSummary& p = v.presets[v.preset_cursor];
    ui::section_title(g, rx, ui::kHeader + 24, p.name);
    std::string about = dreadovania::plain_text(p.description);
    if (about.empty()) {
        about = "No description.";
    }
    std::vector<std::string> lines = g.wrap(about, ui::kBody, rw - 40, 9);
    int lh = g.line_height(ui::kBody);
    int card_h = static_cast<int>(lines.size()) * lh + 36;
    int y = kListTop;
    g.framed(rx, y, rw, card_h, 14, 1, color::line, color::panel);
    int ty = y + 18;
    for (const std::string& line : lines) {
        g.text(line, rx + 20, ty, ui::kBody, color::fg);
        ty += lh;
    }
    y += card_h + 16;
    g.framed(rx, y, rw, 88, 14, 1, color::line, color::panel);
    g.text("Race seed", rx + 20, y + 16, ui::kBody, color::fg, true);
    g.text("No spoiler is kept on the server.", rx + 20, y + 48, ui::kSmall, color::muted);
    ui::toggle(g, rx + rw - 20 - 64, y + 27, v.race);
    y += 88 + 16;
    for (const std::string& line :
         g.wrap("The server generates the seed, patches it with your game files, and "
                   "the app installs it. About a minute or two.",
                   ui::kSmall, rw, 3)) {
        g.text(line, rx, y, ui::kSmall, color::muted);
        y += g.line_height(ui::kSmall);
    }
    ui::footer(g, "Presets are edited on the server",
               {{"A", "Generate and install"}, {"X", "Race on/off"}, {"B", "Back"}});
}

static void draw_working(Gfx& g, const View& v) {
    const int x = 120;
    const int w = ui::Gfx::kWidth - 2 * x;
    int y = ui::kHeader + 40;
    ui::section_title(g, x, y, v.work.title);
    y += 48;
    for (std::size_t i = 0; i < v.work.steps.size(); ++i) {
        const int cy = y + 22;
        bool done = i < v.work.current;
        bool now = i == v.work.current;
        if (done) {
            g.circle(x + 22, cy, 22, color::ok);
            ui::check_mark(g, x + 22, cy, color::bg);
        } else {
            g.ring(x + 22, cy, 22, 3, now ? color::accent : color::line, color::bg);
            g.text(std::to_string(i + 1), x + 22, g.top_for_center(cy, ui::kBody, true), ui::kBody,
                   now ? color::accent : color::muted, true, ui::Align::Center);
        }
        g.text(v.work.steps[i], x + 64, g.top_for_center(cy, 24), 24,
               done || now ? color::fg : color::muted);
        std::string side = now ? v.work.detail : (i == 0 && done ? v.work.seed_hash : "");
        g.text(side, x + w, g.top_for_center(cy, ui::kSmall), ui::kSmall, color::muted, false,
               ui::Align::Right, 520);
        y += 62;
    }
    y += 10;
    if (v.work.fraction >= 0) {
        ui::progress_bar(g, x, y, w, v.work.fraction);
        y += 34;
    }
    g.text(v.work.current >= 2 ? "Every file is checked before anything on the SD card changes."
                                 : "The server keeps working even if you stop waiting.",
              x, y, ui::kSmall, color::muted);
    if (v.work.current >= 2) {
        ui::footer(g, "Keep the app open until it finishes", {});
    } else {
        ui::footer(g, "Keep the app open", {{"B", "Stop waiting (the server carries on)"}});
    }
}

static void draw_pair(Gfx& g, const View& v) {
    const int x = 24;
    const int w = kAsideW - 48;
    int y = ui::kHeader + 24;
    ui::section_title(g, x, y, "Pair this Switch");
    y += 36;
    auto card = [&](const std::string& text) {
        std::vector<std::string> lines = g.wrap(text, ui::kSmall, w - 36, 4);
        int h = static_cast<int>(lines.size()) * g.line_height(ui::kSmall) + 36;
        g.framed(x, y, w, h, 14, 1, color::line, color::panel);
        int ty = y + 18;
        for (const std::string& line : lines) {
            g.text(line, x + 18, ty, ui::kSmall, color::fg);
            ty += g.line_height(ui::kSmall);
        }
        y += h + 16;
    };
    card("1. On your server, open Account > Switch app > Pair a Switch.");
    card("2. Enter the code shown there.");
    std::string ca;
    switch (v.user_ca) {
        case dreadovania::UserCa::Loaded:
            ca = "Your own CA (/config/dreadovania/ca.pem) is trusted too.";
            break;
        case dreadovania::UserCa::NotPem:
            ca = "/config/dreadovania/ca.pem isn't a PEM certificate: ignored.";
            break;
        case dreadovania::UserCa::Missing:
            ca = "Your own CA goes in /config/dreadovania/ca.pem.";
            break;
    }
    for (const std::string& line : g.wrap("Your server must use HTTPS. " + ca, ui::kSmall, w, 5)) {
        g.text(line, x, y, ui::kSmall, color::muted);
        y += g.line_height(ui::kSmall);
    }
    g.box(kAsideW, ui::kHeader, 1, ui::Gfx::kHeight - ui::kHeader - ui::kFooter, 0,
             color::line);

    const int rx = kAsideW + 32;
    const int rw = ui::Gfx::kWidth - rx - 32;
    int ry = ui::kHeader + 24;
    ui::section_title(g, rx, ry, "Server");
    ry += 36;
    g.framed(rx, ry, rw, 68, 14, 1, color::line, color::panel);
    g.text(v.last_server.empty() ? "Not set yet: press A" : v.last_server, rx + 20, ry + 20,
              ui::kBody, v.last_server.empty() ? color::muted : color::fg, false,
              ui::Align::Left, rw - 200);
    if (!v.last_server.empty()) {
        int cw = g.measure("Change", ui::kSmall + 1);
        g.text("Change", rx + rw - 20 - cw, g.top_for_center(ry + 34, ui::kSmall + 1), ui::kSmall + 1,
               color::fg);
        ui::glyph(g, rx + rw - 20 - cw - 38, ry + 34, "Y");
    }
    ry += 68 + 36;
    ui::section_title(g, rx, ry, "Pairing code");
    ry += 40;
    int bx = rx;
    for (std::size_t i = 0; i < 8; ++i) {
        if (i == 4) {
            g.box(bx + 4, ry + 40, 24, 4, 2, color::muted);
            bx += 36;
        }
        bool filled = i < v.typed_code.size();
        bool next = i == v.typed_code.size();
        g.framed(bx, ry, 64, 84, 12, 2, next ? color::accent : color::line, color::panel);
        if (filled) {
            g.text(std::string(1, v.typed_code[i]), bx + 32, g.top_for_center(ry + 42, 40, true), 40,
                   color::fg, true, ui::Align::Center);
        }
        bx += 76;
    }
    ry += 84 + 24;
    g.text("8 letters and numbers, from the server. It expires after 5 minutes.", rx, ry,
              ui::kSmall, color::muted);
    std::vector<ui::Hint> hints = {{"A", v.last_server.empty() ? "Enter the server" : "Type the code"}};
    if (!v.last_server.empty()) {
        hints.push_back({"Y", "Change server"});
    }
    hints.push_back({"+", "Exit"});
    ui::footer(g, "", hints);
}

void draw(Gfx& g, const View& v) {
    g.clear(color::bg);
    std::string right = v.server.empty() ? "Not paired" : v.server;
    if (right.rfind("https://", 0) == 0) {
        right = right.substr(8);
    }
    ui::header(g, right, v.connected);
    switch (v.screen) {
        case Screen::Pair:
            draw_pair(g, v);
            break;
        case Screen::Library:
            draw_library(g, v);
            break;
        case Screen::Presets:
            draw_presets(g, v);
            break;
        case Screen::Working:
            draw_working(g, v);
            break;
    }
    if (v.dialog == Dialog::Replace) {
        std::string example = v.conflicts.empty() ? "" : v.conflicts.front();
        if (std::size_t romfs = example.find("/romfs/"); romfs != std::string::npos) {
            example = example.substr(romfs + 1);  // the part that names the file
        }
        ui::dialog(g, "Replace another mod's files?",
                   {std::to_string(v.conflicts.size()) +
                        " file(s) on the SD card belong to another mod and would be "
                        "replaced, for example " + example + ".",
                    "If they're from a seed you copied by hand, that's fine."},
                   {{"B", "Keep them"}, {"A", "Replace and install"}});
    } else if (v.dialog == Dialog::Unpair) {
        ui::dialog(g, "Unpair this Switch?",
                   {"It will stop reaching your seeds until you pair it again with a new "
                    "code. Installed seeds stay on the SD card."},
                   {{"B", "Keep it"}, {"A", "Unpair"}});
    } else if (v.show_toast && !v.toast.empty()) {
        ui::toast(g, v.toast, v.toast_bad ? color::bad : color::ok);
    }
}

}  // namespace ui
