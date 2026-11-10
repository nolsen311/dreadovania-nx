// SPDX-License-Identifier: GPL-3.0-or-later
// Drawing with SDL2 (ADR 0009): rounded boxes, circles, lines and text in the
// console's own shared font. Everything is in 1280x720 coordinates; docked, SDL
// scales it up.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
typedef struct _TTF_Font TTF_Font;

namespace ui {

struct Color {
    std::uint8_t r, g, b, a = 255;
};

enum class Align { Left, Center, Right };

class Gfx {
  public:
    static constexpr int kWidth = 1280;
    static constexpr int kHeight = 720;

    // Opens the window and loads the font. Returns an error, or "".
    std::string init();
    void shutdown();

    void clear(Color c);
    void present();

    void box(int x, int y, int w, int h, int radius, Color fill);
    // A box with a border `thick` pixels wide.
    void framed(int x, int y, int w, int h, int radius, int thick, Color border, Color fill);
    void circle(int cx, int cy, int r, Color fill);
    void ring(int cx, int cy, int r, int thick, Color border, Color fill);
    void line(int x1, int y1, int x2, int y2, int width, Color c);

    // Draws one line of text with its top at y. Returns its width. Text wider
    // than max_width (if > 0) is cut with "...".
    int text(std::string_view s, int x, int y, int size, Color c, bool bold = false,
             Align align = Align::Left, int max_width = 0);
    int measure(std::string_view s, int size, bool bold = false);
    // Word-wrapped lines that fit `width`; at most `max_lines` (the last one cut).
    std::vector<std::string> wrap(std::string_view s, int size, int width, int max_lines,
                                  bool bold = false);
    int line_height(int size);
#ifndef __SWITCH__
    // tools/preview: saves what was drawn since clear() as a PNG.
    bool save_png(const char* path);
#endif

  private:
    TTF_Font* font(int size, bool bold);
    SDL_Texture* rendered(const std::string& s, int size, Color c, bool bold, int& w, int& h);
    std::string fit(std::string_view s, int size, bool bold, int max_width);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    const void* font_data_ = nullptr;
    std::size_t font_size_ = 0;
    std::map<std::pair<int, bool>, TTF_Font*> fonts_;
    struct Cached {
        SDL_Texture* texture;
        int w, h;
    };
    std::map<std::tuple<std::string, int, std::uint32_t, bool>, Cached> cache_;
};

}  // namespace ui
