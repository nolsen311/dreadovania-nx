// SPDX-License-Identifier: GPL-3.0-or-later
// The app's look (docs/design, ADR 0009): colours, and the pieces every screen
// is built from. Drawn in code; the only art is the project's own hexagon icon.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "dreadovania/api.hpp"
#include "ui/gfx.hpp"

namespace ui {

namespace color {
inline constexpr Color bg{13, 19, 31};
inline constexpr Color panel{22, 31, 47};
inline constexpr Color raised{29, 40, 64};
inline constexpr Color line{42, 52, 71};
inline constexpr Color fg{231, 235, 243};
inline constexpr Color muted{154, 164, 181};
inline constexpr Color accent{232, 89, 12};
inline constexpr Color ok{64, 192, 87};
inline constexpr Color wait{77, 171, 247};
inline constexpr Color bad{255, 107, 107};
inline constexpr Color white{255, 255, 255};
inline constexpr Color shade{5, 8, 14, 184};
}  // namespace color

inline constexpr int kHeader = 72;
inline constexpr int kFooter = 64;
inline constexpr int kBody = 22;   // body text size
inline constexpr int kSmall = 18;

Color tone_color(dreadovania::Tone tone);

using Hint = std::pair<std::string, std::string>;  // button, what it does

void logo(Gfx& g, int x, int y, int size);
// The top bar. ``button``, if given, is drawn after the server address: the
// button that changes it (L opens the Servers screen, ADR 0010).
void header(Gfx& g, const std::string& right, bool connected, const std::string& button = "");
void footer(Gfx& g, const std::string& left, const std::vector<Hint>& hints);
// A controller button's letter in a circle (or a pill for wider labels). Returns its width.
// A controller button: a circle for A, B, X, Y, + and -; the shape of a
// shoulder button for L, R, ZL and ZR. Returns its width.
int glyph(Gfx& g, int x, int cy, const std::string& label);
int glyph_width(Gfx& g, const std::string& label);
// A chip whose right edge is at `right`. Returns its width.
int chip(Gfx& g, int right, int cy, const std::string& label, Color c);
void section_title(Gfx& g, int x, int y, const std::string& text);
// A list row: selected rows get the accent border.
void row_box(Gfx& g, int x, int y, int w, int h, bool selected);
void check_mark(Gfx& g, int cx, int cy, Color c);
void progress_bar(Gfx& g, int x, int y, int w, double fraction);
void toggle(Gfx& g, int x, int y, bool on);
// A centred dialog over a shaded screen.
void dialog(Gfx& g, const std::string& title, const std::vector<std::string>& lines,
            const std::vector<Hint>& actions);
void toast(Gfx& g, const std::string& text, Color c);

}  // namespace ui
