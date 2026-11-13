// SPDX-License-Identifier: GPL-3.0-or-later
#include "updater.hpp"

#include <cstdio>

#include "dreadovania/sha256.hpp"

namespace app {

namespace {

std::string explain(const Response& r, const char* fallback) {
    if (!r.error.empty()) {
        return r.error;
    }
    auto detail = dreadovania::problem_detail(r.body);
    return (detail ? *detail : std::string(fallback)) + " (HTTP " + std::to_string(r.status) + ")";
}

}  // namespace

bool updatable_path(const std::string& path) {
    const std::string sd = "sdmc:/";
    const std::string nro = ".nro";
    return path.size() > sd.size() + nro.size() && path.compare(0, sd.size(), sd) == 0 &&
           path.compare(path.size() - nro.size(), nro.size(), nro) == 0;
}

UpdateResult download_update(Http& http, const std::string& server, const std::string& token,
                             const dreadovania::AppLatest& latest, const std::string& self_path,
                             const std::function<void(double)>& progress,
                             const std::function<void()>& on_replace) {
    UpdateResult result;
    if (!updatable_path(self_path)) {
        result.message = "This copy of the app can't update itself. Download " + latest.version +
                         " from the app's Releases page and copy it to the SD card.";
        return result;
    }
    const std::string fresh = self_path + ".new";
    const std::string old = self_path + ".old";
    std::remove(fresh.c_str());
    std::FILE* out = std::fopen(fresh.c_str(), "wb");
    if (out == nullptr) {
        result.message = "Couldn't write to the SD card. Nothing was changed.";
        return result;
    }
    dreadovania::Sha256 hash;
    std::uint64_t received = 0;
    bool too_big = false;
    bool write_failed = false;
    Response r = http.download(
        server + "/api/v1/app/dreadovania.nro", token,
        [&](const char* data, std::size_t n) {
            if (received + n > latest.size) {
                too_big = true;
                return false;
            }
            if (std::fwrite(data, 1, n, out) != n) {
                write_failed = true;
                return false;
            }
            hash.update(data, n);
            received += n;
            return true;
        },
        [&](std::uint64_t got) {
            if (progress) {
                progress(static_cast<double>(got) / static_cast<double>(latest.size));
            }
        });
    bool closed = std::fclose(out) == 0;
    auto fail = [&](const std::string& why) {
        std::remove(fresh.c_str());
        result.message = "Not updated: " + why + ". Nothing was changed.";
        return result;
    };
    if (write_failed || !closed) {
        return fail("couldn't write to the SD card");
    }
    if (r.status != 200 || (!r.error.empty() && !too_big)) {
        return fail(explain(r, "the download failed"));
    }
    if (too_big || received != latest.size) {
        return fail("the download wasn't the expected size");
    }
    if (hash.hex() != latest.sha256) {
        return fail("the download didn't match its checksum");
    }
    if (on_replace) {
        on_replace();  // checked: the screen moves on to "Replace the app"
    }
    // Swap: keep the old file until the new one is in place.
    std::remove(old.c_str());
    if (std::rename(self_path.c_str(), old.c_str()) != 0) {
        return fail("couldn't replace the app on the SD card");
    }
    if (std::rename(fresh.c_str(), self_path.c_str()) != 0) {
        std::rename(old.c_str(), self_path.c_str());  // put the old app back
        return fail("couldn't replace the app on the SD card");
    }
    std::remove(old.c_str());
    result.ok = true;
    result.message = "Updated to " + latest.version + ".";
    return result;
}

}  // namespace app
