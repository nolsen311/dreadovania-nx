// SPDX-License-Identifier: GPL-3.0-or-later
#include "dreadovania/install.hpp"

#include <algorithm>
#include <cctype>

#include "dreadovania/json.hpp"
#include "dreadovania/paths.hpp"

namespace dreadovania {

namespace {

constexpr std::size_t kBlock = 512;

bool is_sha256(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

std::string field(const char* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len] != '\0') {
        ++len;
    }
    return std::string(p, len);
}

std::optional<std::uint64_t> octal(const char* p, std::size_t n) {
    std::uint64_t value = 0;
    bool any = false;
    for (std::size_t i = 0; i < n; ++i) {
        char c = p[i];
        if (c == '\0' || c == ' ') {
            if (any) {
                break;
            }
            continue;
        }
        if (c < '0' || c > '7' || value > (UINT64_MAX >> 3)) {
            return std::nullopt;
        }
        value = value * 8 + static_cast<std::uint64_t>(c - '0');
        any = true;
    }
    return any ? std::optional<std::uint64_t>(value) : std::nullopt;
}

}  // namespace

std::optional<ModManifest> parse_mod_manifest(std::string_view body, std::string& error) {
    json::ParseResult parsed = json::parse(body);
    if (!parsed.value || !parsed.value->is_object()) {
        error = "the server's file list isn't valid";
        return std::nullopt;
    }
    const json::Value* files = parsed.value->get("files");
    auto tar = parsed.value->int_at("tar_bytes");
    if (files == nullptr || !files->is_array() || !tar || *tar < 1024) {
        error = "the server's file list isn't valid";
        return std::nullopt;
    }
    ModManifest manifest;
    manifest.tar_bytes = static_cast<std::uint64_t>(*tar);
    std::set<std::string> seen;
    for (const json::Value& item : files->items()) {
        auto path = item.is_object() ? item.string_at("path") : std::nullopt;
        auto size = item.is_object() ? item.int_at("size") : std::nullopt;
        auto sha = item.is_object() ? item.string_at("sha256") : std::nullopt;
        if (!path || !size || *size < 0 || !sha || !is_sha256(*sha)) {
            error = "the server's file list isn't valid";
            return std::nullopt;
        }
        std::string sd_path = "/" + *path;
        if (!in_mod_roots(sd_path) || normalize_sd_path(sd_path) != sd_path ||
            !seen.insert(sd_path).second) {
            error = "the server's file list has a path outside Dread's mod folders";
            return std::nullopt;
        }
        manifest.files.push_back({sd_path, static_cast<std::uint64_t>(*size), *sha});
        manifest.size_bytes += static_cast<std::uint64_t>(*size);
    }
    if (manifest.files.empty()) {
        error = "the patched seed has no files";
        return std::nullopt;
    }
    return manifest;
}

bool TarVerifier::fail(std::string message) {
    if (state_ != State::Failed) {
        error_ = std::move(message);
        state_ = State::Failed;
    }
    return false;
}

bool TarVerifier::header(const char* b) {
    bool all_zero = std::all_of(b, b + kBlock, [](char c) { return c == '\0'; });
    if (all_zero) {
        if (index_ != manifest_.files.size()) {
            return fail("the download ended before every file arrived");
        }
        state_ = State::End;
        zero_blocks_ = 1;
        return true;
    }
    if (index_ >= manifest_.files.size()) {
        return fail("the download has files the server's list doesn't");
    }
    if (field(b + 257, 6) != "ustar") {
        return fail("the download isn't a tar archive");
    }
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kBlock; ++i) {
        sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(b[i]);
    }
    if (octal(b + 148, 8) != sum) {
        return fail("the download is damaged (bad tar header)");
    }
    char type = b[156];
    if (type != '0' && type != '\0') {
        return fail("the download has something other than a plain file");
    }
    std::string prefix = field(b + 345, 155);
    std::string name = (prefix.empty() ? "" : prefix + "/") + field(b, 100);
    auto size = octal(b + 124, 12);
    const ModFile& expected = manifest_.files[index_];
    if ("/" + name != expected.sd_path || !size || *size != expected.size) {
        return fail("the download doesn't match the server's file list (" + name + ")");
    }
    hash_ = Sha256();
    remaining_ = *size;
    padding_ = (kBlock - (*size % kBlock)) % kBlock;
    if (on_file_start && !on_file_start(expected)) {
        return fail("couldn't write " + expected.sd_path);
    }
    state_ = State::Data;
    if (remaining_ == 0) {
        if (hash_.hex() != expected.sha256) {
            return fail("a file is damaged (" + expected.sd_path + ")");
        }
        if (on_file_end && !on_file_end(expected)) {
            return fail("couldn't write " + expected.sd_path);
        }
        ++index_;
        state_ = State::Header;
    }
    return true;
}

bool TarVerifier::feed(const char* data, std::size_t size) {
    std::size_t pos = 0;
    while (pos < size) {
        switch (state_) {
            case State::Failed:
                return false;
            case State::Done:
                return fail("the download has data after the end of the archive");
            case State::Header:
            case State::End: {
                std::size_t take = std::min(size - pos, kBlock - block_.size());
                block_.append(data + pos, take);
                pos += take;
                if (block_.size() < kBlock) {
                    break;
                }
                std::string block = std::move(block_);
                block_.clear();
                if (state_ == State::End) {
                    if (!std::all_of(block.begin(), block.end(), [](char c) { return c == '\0'; })) {
                        return fail("the download has data after the end of the archive");
                    }
                    if (++zero_blocks_ == 2) {
                        state_ = State::Done;
                    }
                } else if (!header(block.data())) {
                    return false;
                }
                break;
            }
            case State::Data: {
                std::size_t take = static_cast<std::size_t>(
                    std::min<std::uint64_t>(size - pos, remaining_));
                hash_.update(data + pos, take);
                if (on_data && !on_data(data + pos, take)) {
                    return fail("couldn't write " + manifest_.files[index_].sd_path);
                }
                pos += take;
                remaining_ -= take;
                if (remaining_ == 0) {
                    const ModFile& file = manifest_.files[index_];
                    if (hash_.hex() != file.sha256) {
                        return fail("a file is damaged (" + file.sd_path + ")");
                    }
                    if (on_file_end && !on_file_end(file)) {
                        return fail("couldn't write " + file.sd_path);
                    }
                    ++index_;
                    state_ = padding_ ? State::Padding : State::Header;
                }
                break;
            }
            case State::Padding: {
                std::size_t take =
                    static_cast<std::size_t>(std::min<std::uint64_t>(size - pos, padding_));
                pos += take;
                padding_ -= take;
                if (padding_ == 0) {
                    state_ = State::Header;
                }
                break;
            }
        }
    }
    return state_ != State::Failed;
}

std::vector<std::string> parse_sd_manifest(std::string_view text) {
    std::vector<std::string> out;
    json::ParseResult parsed = json::parse(text);
    if (!parsed.value || !parsed.value->is_object()) {
        return out;
    }
    const json::Value* files = parsed.value->get("files");
    if (files == nullptr || !files->is_array()) {
        return out;
    }
    for (const json::Value& f : files->items()) {
        if (f.is_string() && in_mod_roots(f.as_string())) {
            out.push_back(*normalize_sd_path(f.as_string()));
        }
    }
    return out;
}

std::optional<std::string> sd_manifest_seed(std::string_view text) {
    json::ParseResult parsed = json::parse(text);
    if (!parsed.value || !parsed.value->is_object() ||
        parsed.value->string_at("state") != "installed") {
        return std::nullopt;
    }
    return parsed.value->string_at("seed_id");
}

std::string sd_manifest_json(const std::vector<std::string>& files, std::string_view seed_id,
                             std::string_view state) {
    std::string out = "{\n \"tool\": \"dreadovania\",\n \"seed_id\": " + json::quote(seed_id) +
                      ",\n \"state\": " + json::quote(state) + ",\n \"files\": [";
    for (std::size_t i = 0; i < files.size(); ++i) {
        out += (i ? ",\n  " : "\n  ") + json::quote(files[i]);
    }
    out += files.empty() ? "]\n}\n" : "\n ]\n}\n";
    return out;
}

InstallPlan plan_install(const ModManifest& manifest, const std::vector<std::string>& owned,
                         const std::function<bool(const std::string&)>& exists) {
    InstallPlan plan;
    std::set<std::string> owned_set;
    for (const std::string& p : owned) {
        if (in_mod_roots(p)) {
            owned_set.insert(p);
        }
    }
    std::set<std::string> planned;
    for (const ModFile& f : manifest.files) {
        plan.files.push_back(f.sd_path);
        planned.insert(f.sd_path);
        if (!owned_set.count(f.sd_path) && exists(f.sd_path)) {
            plan.conflicts.push_back(f.sd_path);
        }
    }
    for (const std::string& p : owned_set) {
        if (!planned.count(p)) {
            plan.remove.push_back(p);
        }
    }
    std::set<std::string> record = owned_set;
    record.insert(planned.begin(), planned.end());
    plan.record.assign(record.begin(), record.end());
    return plan;
}

std::vector<std::string> folders_to_tidy(const std::vector<std::string>& removed) {
    static const std::set<std::string> keep = {"/", "/atmosphere", "/atmosphere/contents",
                                               "/atmosphere/exefs_patches"};
    std::set<std::string> dirs;
    for (const std::string& path : removed) {
        std::string parent = path.substr(0, path.rfind('/'));
        while (!parent.empty() && !keep.count(parent) && dirs.insert(parent).second) {
            parent = parent.substr(0, parent.rfind('/'));
        }
    }
    std::vector<std::string> out(dirs.begin(), dirs.end());
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        auto depth = [](const std::string& s) { return std::count(s.begin(), s.end(), '/'); };
        return depth(a) != depth(b) ? depth(a) > depth(b) : a < b;
    });
    return out;
}

}  // namespace dreadovania
