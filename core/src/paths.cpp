// SPDX-License-Identifier: GPL-3.0-or-later
#include "dreadovania/paths.hpp"

#include <vector>

namespace dreadovania {

std::optional<std::string> normalize_sd_path(std::string_view path) {
    if (path.empty() || path.front() != '/') {
        return std::nullopt;
    }
    for (char c : path) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f || c == '\\') {
            return std::nullopt;
        }
    }
    std::vector<std::string_view> parts;
    std::size_t start = 1;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        std::string_view part = path.substr(start, end - start);
        if (part == "..") {
            return std::nullopt;  // never resolved: a manifest could be crafted
        }
        if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        start = end + 1;
    }
    std::string out;
    for (std::string_view part : parts) {
        out += '/';
        out += part;
    }
    return out.empty() ? std::string("/") : out;
}

bool in_mod_roots(std::string_view path) {
    std::optional<std::string> normal = normalize_sd_path(path);
    if (!normal) {
        return false;
    }
    for (std::string_view root : kModRoots) {
        if (normal->size() > root.size() + 1 && normal->compare(0, root.size(), root) == 0 &&
            (*normal)[root.size()] == '/') {
            return true;
        }
    }
    return false;
}

}  // namespace dreadovania
