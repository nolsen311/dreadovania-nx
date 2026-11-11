// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>

namespace ui {

Color tone_color(dreadovania::Tone tone) {
    switch (tone) {
        case dreadovania::Tone::Ok:
            return color::ok;
        case dreadovania::Tone::Wait:
            return color::wait;
        case dreadovania::Tone::Bad:
            return color::bad;
        case dreadovania::Tone::Accent:
            return color::accent;
        case dreadovania::Tone::Muted:
            break;
    }
    return color::muted;
}

void logo(Gfx& g, int x, int y, int size) {
    // The project's icon: a hexagon with a "D", as on the server.
    const double cx = x + size / 2.0;
    const double cy = y + size / 2.0;
    const double r = size * 0.45;
    for (int i = 0; i < 6; ++i) {
        double a1 = (60.0 * i - 90.0) * M_PI / 180.0;
        double a2 = (60.0 * (i + 1) - 90.0) * M_PI / 180.0;
        g.line(static_cast<int>(cx + r * std::cos(a1)), static_cast<int>(cy + r * std::sin(a1)),
               static_cast<int>(cx + r * std::cos(a2)), static_cast<int>(cy + r * std::sin(a2)),
               3, color::accent);
    }
    g.text("D", static_cast<int>(cx), g.top_for_center(static_cast<int>(cy), size / 2, true),
           size / 2, color::fg, true, Align::Center);
}

void header(Gfx& g, const std::string& right, bool connected) {
    logo(g, 32, 16, 40);
    g.text("Dreadovania", 88, 20, 26, color::fg, true);
    int w = g.text(right, Gfx::kWidth - 32, 25, kSmall, color::muted, false, Align::Right, 560);
    if (connected && w > 0) {
        g.circle(Gfx::kWidth - 32 - w - 16, 36, 5, color::ok);
    }
    g.box(0, kHeader - 1, Gfx::kWidth, 1, 0, color::line);
}

int glyph(Gfx& g, int x, int cy, const std::string& label) {
    int tw = g.measure(label, 17, true);
    int w = label.size() <= 1 ? 30 : tw + 20;
    g.box(x, cy - 15, w, 30, 15, color::fg);
    g.text(label, x + w / 2, g.top_for_center(cy, 17, true), 17, color::bg, true, Align::Center);
    return w;
}

void footer(Gfx& g, const std::string& left, const std::vector<Hint>& hints) {
    int top = Gfx::kHeight - kFooter;
    g.box(0, top, Gfx::kWidth, 1, 0, color::line);
    int cy = top + kFooter / 2;
    const int label_top = g.top_for_center(cy, kSmall + 1);
    int hints_w = 0;
    for (const Hint& h : hints) {
        int gw = h.first.size() <= 1 ? 30 : g.measure(h.first, 17, true) + 20;
        hints_w += g.measure(h.second, kSmall + 1) + gw + 8 + 28;
    }
    // The left text gets what the hints leave, so the two never overlap.
    int left_w = std::max(1, Gfx::kWidth - 32 - hints_w - 32);  // 0 would mean no limit
    g.text(left, 32, label_top, kSmall + 1, color::muted, false, Align::Left, left_w);
    int x = Gfx::kWidth - 32;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        int label_w = g.measure(it->second, kSmall + 1);
        x -= label_w;
        g.text(it->second, x, label_top, kSmall + 1, color::fg);
        int gw = it->first.size() <= 1 ? 30 : g.measure(it->first, 17, true) + 20;
        x -= gw + 8;
        glyph(g, x, cy, it->first);
        x -= 28;
    }
}

int chip(Gfx& g, int right, int cy, const std::string& label, Color c) {
    std::string upper;
    for (char ch : label) {
        upper += static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    }
    int w = g.measure(upper, 15, true) + 28;
    g.framed(right - w, cy - 16, w, 32, 16, 2, c, color::panel);
    g.text(upper, right - w / 2, g.top_for_center(cy, 15, true), 15, c, true, Align::Center);
    return w;
}

void section_title(Gfx& g, int x, int y, const std::string& text) {
    std::string upper;
    for (char ch : text) {
        upper += static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    }
    g.text(upper, x, y, kSmall, color::muted, true);
}

void row_box(Gfx& g, int x, int y, int w, int h, bool selected) {
    if (selected) {
        g.box(x - 4, y - 4, w + 8, h + 8, 16, Color{232, 89, 12, 46});  // the glow
        g.framed(x, y, w, h, 12, 2, color::accent, color::raised);
    } else {
        g.box(x, y, w, h, 12, color::panel);
    }
}

void check_mark(Gfx& g, int cx, int cy, Color c) {
    g.line(cx - 8, cy, cx - 2, cy + 6, 4, c);
    g.line(cx - 2, cy + 6, cx + 9, cy - 6, 4, c);
}

void progress_bar(Gfx& g, int x, int y, int w, double fraction) {
    g.box(x, y, w, 14, 7, color::line);
    if (fraction > 0) {
        int filled = static_cast<int>(w * (fraction > 1 ? 1 : fraction));
        g.box(x, y, filled < 14 ? 14 : filled, 14, 7, color::accent);
    }
}

void toggle(Gfx& g, int x, int y, bool on) {
    g.box(x, y, 64, 34, 17, on ? color::accent : color::line);
    g.circle(on ? x + 47 : x + 17, y + 17, 13, color::fg);
}

void dialog(Gfx& g, const std::string& title, const std::vector<std::string>& lines,
            const std::vector<Hint>& actions) {
    g.box(0, 0, Gfx::kWidth, Gfx::kHeight, 0, color::shade);
    const int w = 720;
    std::vector<std::string> wrapped;
    for (const std::string& line : lines) {
        for (std::string& part : g.wrap(line, kBody, w - 64, 4)) {
            wrapped.push_back(std::move(part));
        }
    }
    const int lh = g.line_height(kBody);
    const int h = 32 + 40 + 16 + static_cast<int>(wrapped.size()) * lh + 24 + 52 + 32;
    const int x = (Gfx::kWidth - w) / 2;
    const int y = (Gfx::kHeight - h) / 2;
    g.framed(x, y, w, h, 18, 1, color::line, color::panel);
    g.text(title, x + 32, y + 32, 28, color::fg, true, Align::Left, w - 64);
    int ty = y + 32 + 40 + 16;
    for (const std::string& line : wrapped) {
        g.text(line, x + 32, ty, kBody, color::fg);
        ty += lh;
    }
    int bx = x + w - 32;
    int by = y + h - 32 - 52;
    for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
        bool primary = it == actions.rbegin();
        int bw = g.measure(it->second, kBody, true) + 30 + 10 + 52;
        bx -= bw;
        if (primary) {
            g.box(bx, by, bw, 52, 12, color::accent);
        } else {
            g.framed(bx, by, bw, 52, 12, 2, color::line, color::panel);
        }
        glyph(g, bx + 26, by + 26, it->first);
        g.text(it->second, bx + 26 + 30 + 10, g.top_for_center(by + 26, kBody, true), kBody,
               primary ? color::white : color::fg, true);
        bx -= 16;
    }
}

void toast(Gfx& g, const std::string& text, Color c) {
    const int max_w = 1100;
    std::vector<std::string> lines = g.wrap(text, kSmall + 1, max_w - 70, 3);
    int w = 0;
    for (const std::string& line : lines) {
        int lw = g.measure(line, kSmall + 1);
        w = lw > w ? lw : w;
    }
    w += 70;
    const int lh = g.line_height(kSmall + 1);
    const int h = static_cast<int>(lines.size()) * lh + 28;
    const int x = (Gfx::kWidth - w) / 2;
    const int y = Gfx::kHeight - kFooter - 20 - h;
    g.framed(x, y, w, h, 12, 1, c, color::raised);
    g.circle(x + 26, y + h / 2, 6, c);
    int ty = y + 14;
    for (const std::string& line : lines) {
        g.text(line, x + 46, ty, kSmall + 1, color::fg);
        ty += lh;
    }
}

}  // namespace ui
