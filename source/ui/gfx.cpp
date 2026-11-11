// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/gfx.hpp"

#include <SDL.h>
#include <SDL2_gfxPrimitives.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#else
#include <SDL_image.h>
#endif

namespace ui {

namespace {

constexpr std::size_t kMaxCached = 400;  // rendered strings kept as textures

std::uint32_t pack(Color c) {
    return (std::uint32_t{c.r} << 24) | (std::uint32_t{c.g} << 16) | (std::uint32_t{c.b} << 8) | c.a;
}

}  // namespace

std::string Gfx::init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return std::string("the screen couldn't start: ") + SDL_GetError();
    }
    window_ = SDL_CreateWindow("Dreadovania", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               kWidth, kHeight, 0);
    if (window_ != nullptr) {
        renderer_ = SDL_CreateRenderer(window_, -1,
                                       SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    }
    if (renderer_ == nullptr) {
        return std::string("the screen couldn't start: ") + SDL_GetError();
    }
    SDL_RenderSetLogicalSize(renderer_, kWidth, kHeight);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (TTF_Init() != 0) {
        return std::string("text couldn't be drawn: ") + TTF_GetError();
    }
#ifdef __SWITCH__
    // The console's own font: nothing to bundle, and it covers accents.
    PlFontData data{};
    if (R_FAILED(plInitialize(PlServiceType_User)) ||
        R_FAILED(plGetSharedFontByType(&data, PlSharedFontType_Standard))) {
        return "the system font couldn't be loaded";
    }
    font_data_ = data.address;
    font_size_ = data.size;
#else
    // tools/preview: a font file from the computer, named by DREADOVANIA_FONT.
    const char* path = std::getenv("DREADOVANIA_FONT");
    std::FILE* file = path != nullptr ? std::fopen(path, "rb") : nullptr;
    if (file == nullptr) {
        return "set DREADOVANIA_FONT to a .ttf or .otf file";
    }
    static std::vector<unsigned char> bytes;
    int c = 0;
    while ((c = std::fgetc(file)) != EOF) {
        bytes.push_back(static_cast<unsigned char>(c));
    }
    std::fclose(file);
    font_data_ = bytes.data();
    font_size_ = bytes.size();
#endif
    return "";
}

void Gfx::shutdown() {
    for (auto& [key, cached] : cache_) {
        SDL_DestroyTexture(cached.texture);
    }
    cache_.clear();
    for (auto& [key, f] : fonts_) {
        TTF_CloseFont(f);
    }
    fonts_.clear();
    if (font_data_ != nullptr) {
#ifdef __SWITCH__
        plExit();
#endif
        font_data_ = nullptr;
    }
    TTF_Quit();
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
}

void Gfx::clear(Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_RenderClear(renderer_);
}

void Gfx::present() { SDL_RenderPresent(renderer_); }

void Gfx::box(int x, int y, int w, int h, int radius, Color c) {
    if (w <= 0 || h <= 0) {
        return;
    }
    radius = std::min({radius, w / 2, h / 2});
    if (radius <= 0) {
        boxRGBA(renderer_, static_cast<Sint16>(x), static_cast<Sint16>(y),
                static_cast<Sint16>(x + w - 1), static_cast<Sint16>(y + h - 1), c.r, c.g, c.b, c.a);
        return;
    }
    roundedBoxRGBA(renderer_, static_cast<Sint16>(x), static_cast<Sint16>(y),
                   static_cast<Sint16>(x + w - 1), static_cast<Sint16>(y + h - 1),
                   static_cast<Sint16>(radius), c.r, c.g, c.b, c.a);
}

void Gfx::framed(int x, int y, int w, int h, int radius, int thick, Color border, Color fill) {
    box(x, y, w, h, radius, border);
    box(x + thick, y + thick, w - 2 * thick, h - 2 * thick, std::max(0, radius - thick), fill);
}

void Gfx::circle(int cx, int cy, int r, Color c) {
    filledCircleRGBA(renderer_, static_cast<Sint16>(cx), static_cast<Sint16>(cy),
                     static_cast<Sint16>(r), c.r, c.g, c.b, c.a);
    aacircleRGBA(renderer_, static_cast<Sint16>(cx), static_cast<Sint16>(cy),
                 static_cast<Sint16>(r), c.r, c.g, c.b, c.a);
}

void Gfx::ring(int cx, int cy, int r, int thick, Color border, Color fill) {
    circle(cx, cy, r, border);
    circle(cx, cy, r - thick, fill);
}

void Gfx::line(int x1, int y1, int x2, int y2, int width, Color c) {
    thickLineRGBA(renderer_, static_cast<Sint16>(x1), static_cast<Sint16>(y1),
                  static_cast<Sint16>(x2), static_cast<Sint16>(y2), static_cast<Uint8>(width),
                  c.r, c.g, c.b, c.a);
}

TTF_Font* Gfx::font(int size, bool bold) {
    auto key = std::make_pair(size, bold);
    if (auto it = fonts_.find(key); it != fonts_.end()) {
        return it->second;
    }
    TTF_Font* f = TTF_OpenFontRW(SDL_RWFromConstMem(font_data_, static_cast<int>(font_size_)), 1, size);
    if (f != nullptr && bold) {
        TTF_SetFontStyle(f, TTF_STYLE_BOLD);
    }
    fonts_[key] = f;
    return f;
}

int Gfx::line_height(int size) {
    TTF_Font* f = font(size, false);
    return f != nullptr ? TTF_FontLineSkip(f) : size + size / 3;
}

int Gfx::top_for_center(int cy, int size, bool bold) {
    TTF_Font* f = font(size, bold);
    if (f == nullptr) {
        return cy - size / 2;
    }
    int minx = 0, maxx = 0, miny = 0, maxy = 0, advance = 0;
    int cap = TTF_GlyphMetrics(f, 'H', &minx, &maxx, &miny, &maxy, &advance) == 0 ? maxy
                                                                                : size * 7 / 10;
    // A rendered line's top is `ascent` above the baseline; capitals reach `cap` above it.
    return cy - TTF_FontAscent(f) + cap / 2;
}

int Gfx::measure(std::string_view s, int size, bool bold) {
    TTF_Font* f = font(size, bold);
    int w = 0;
    int h = 0;
    if (f == nullptr || s.empty() || TTF_SizeUTF8(f, std::string(s).c_str(), &w, &h) != 0) {
        return 0;
    }
    return w;
}

std::string Gfx::fit(std::string_view s, int size, bool bold, int max_width) {
    std::string out(s);
    if (max_width <= 0 || measure(out, size, bold) <= max_width) {
        return out;
    }
    while (!out.empty() && measure(out + "...", size, bold) > max_width) {
        out.pop_back();
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
            out.pop_back();  // never leave half a UTF-8 character
        }
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80) != 0) {
            out.pop_back();
        }
    }
    return out + "...";
}

SDL_Texture* Gfx::rendered(const std::string& s, int size, Color c, bool bold, int& w, int& h) {
    auto key = std::make_tuple(s, size, pack(c), bold);
    if (auto it = cache_.find(key); it != cache_.end()) {
        w = it->second.w;
        h = it->second.h;
        return it->second.texture;
    }
    if (cache_.size() >= kMaxCached) {
        for (auto& [k, cached] : cache_) {
            SDL_DestroyTexture(cached.texture);
        }
        cache_.clear();
    }
    TTF_Font* f = font(size, bold);
    if (f == nullptr) {
        return nullptr;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Blended(f, s.c_str(), SDL_Color{c.r, c.g, c.b, c.a});
    if (surface == nullptr) {
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    w = surface->w;
    h = surface->h;
    SDL_FreeSurface(surface);
    if (texture != nullptr) {
        cache_[key] = Cached{texture, w, h};
    }
    return texture;
}

int Gfx::text(std::string_view s, int x, int y, int size, Color c, bool bold, Align align,
              int max_width) {
    if (s.empty()) {
        return 0;
    }
    std::string shown = fit(s, size, bold, max_width);
    int w = 0;
    int h = 0;
    SDL_Texture* texture = rendered(shown, size, c, bold, w, h);
    if (texture == nullptr) {
        return 0;
    }
    int left = align == Align::Left ? x : align == Align::Center ? x - w / 2 : x - w;
    SDL_Rect dest{left, y, w, h};
    SDL_RenderCopy(renderer_, texture, nullptr, &dest);
    return w;
}

std::vector<std::string> Gfx::wrap(std::string_view s, int size, int width, int max_lines,
                                   bool bold) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= s.size() && static_cast<int>(lines.size()) < max_lines) {
        std::size_t end = s.find('\n', start);
        std::string_view paragraph = s.substr(start, end == std::string_view::npos ? end : end - start);
        std::string current;
        std::size_t pos = 0;
        while (pos <= paragraph.size()) {
            std::size_t space = paragraph.find(' ', pos);
            std::string_view word = paragraph.substr(pos, space == std::string_view::npos ? space : space - pos);
            // A word wider than the line (a long path) is broken where it has to be.
            while (measure(word, size, bold) > width && word.size() > 1) {
                std::size_t cut = word.size() - 1;
                while (cut > 1 && measure(word.substr(0, cut), size, bold) > width) {
                    --cut;
                }
                while (cut > 1 && (static_cast<unsigned char>(word[cut]) & 0xC0) == 0x80) {
                    --cut;  // not inside a UTF-8 character
                }
                if (!current.empty()) {
                    lines.push_back(current);
                    current.clear();
                }
                lines.emplace_back(word.substr(0, cut));
                word.remove_prefix(cut);
            }
            std::string candidate = current.empty() ? std::string(word) : current + " " + std::string(word);
            if (!current.empty() && measure(candidate, size, bold) > width) {
                lines.push_back(current);
                current = std::string(word);
            } else {
                current = candidate;
            }
            if (space == std::string_view::npos) {
                break;
            }
            pos = space + 1;
        }
        lines.push_back(current);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    if (static_cast<int>(lines.size()) > max_lines) {
        lines.resize(static_cast<std::size_t>(max_lines));
    }
    if (static_cast<int>(lines.size()) == max_lines && max_lines > 0) {
        lines.back() = fit(lines.back(), size, bold, width);
    }
    return lines;
}

#ifndef __SWITCH__
bool Gfx::save_png(const char* path) {
    SDL_Surface* surface =
        SDL_CreateRGBSurfaceWithFormat(0, kWidth, kHeight, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) {
        return false;
    }
    bool ok = SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels,
                                   surface->pitch) == 0 &&
              IMG_SavePNG(surface, path) == 0;
    SDL_FreeSurface(surface);
    return ok;
}
#endif

}  // namespace ui
