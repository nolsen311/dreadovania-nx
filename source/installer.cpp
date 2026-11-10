// SPDX-License-Identifier: GPL-3.0-or-later
#include "installer.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "dreadovania/api.hpp"
#include "dreadovania/install.hpp"
#include "dreadovania/paths.hpp"

namespace app {

namespace {

constexpr const char* kStaging = "sdmc:/config/dreadovania/staging";
const std::string kSd = "sdmc:";

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

bool exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

void make_dirs(const std::string& dir) {  // like mkdir -p, for "sdmc:/a/b/c"
    for (std::size_t pos = kSd.size() + 1; pos != std::string::npos;) {
        pos = dir.find('/', pos + 1);
        mkdir(dir.substr(0, pos).c_str(), 0777);
    }
}

void remove_tree(const std::string& dir, int depth = 0) {  // only ever the staging folder
    if (depth > 32) {
        return;
    }
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string name = e->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            std::string path = dir + "/" + name;
            if (e->d_type == DT_DIR) {
                remove_tree(path, depth + 1);
            } else {
                std::remove(path.c_str());
            }
        }
        closedir(d);
    }
    rmdir(dir.c_str());
}

std::string mb(std::uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

std::string explain(const Response& r, const char* fallback) {
    if (!r.error.empty()) {
        return r.error;
    }
    auto detail = dreadovania::problem_detail(r.body);
    return (detail ? *detail : std::string(fallback)) + " (HTTP " + std::to_string(r.status) + ")";
}

}  // namespace

std::optional<std::string> installed_seed() {
    return dreadovania::sd_manifest_seed(read_text(kSd + std::string(dreadovania::kSdManifest)));
}

InstallResult install_seed(Http& http, const std::string& server, const std::string& token,
                           const std::string& seed_id, bool overwrite,
                           const std::function<void(const std::string&)>& status) {
    InstallResult result;
    const std::string base = server + "/api/v1/seeds/" + seed_id;

    // 1. What the seed contains.
    status("Getting the list of files...");
    Response listing = http.request("GET", base + "/mod/manifest", "", token);
    if (listing.status != 200) {
        result.message = "Not installed: " + explain(listing, "the server refused");
        return result;
    }
    std::string error;
    auto manifest = dreadovania::parse_mod_manifest(listing.body, error);
    if (!manifest) {
        result.message = "Not installed: " + error + ".";
        return result;
    }

    // 2. Download into staging, checking every file as it arrives.
    remove_tree(kStaging);
    make_dirs(kStaging);
    dreadovania::TarVerifier verifier(*manifest);
    FILE* out = nullptr;
    verifier.on_file_start = [&](const dreadovania::ModFile& f) {
        std::string path = kStaging + f.sd_path;
        make_dirs(path.substr(0, path.rfind('/')));
        out = std::fopen(path.c_str(), "wb");
        return out != nullptr;
    };
    verifier.on_data = [&](const char* d, std::size_t n) {
        return std::fwrite(d, 1, n, out) == n;
    };
    verifier.on_file_end = [&](const dreadovania::ModFile&) {
        bool ok = std::fclose(out) == 0;
        out = nullptr;
        return ok;
    };
    std::uint64_t last_shown = 0;
    Response tar = http.download(
        base + "/mod.tar", token,
        [&](const char* d, std::size_t n) { return verifier.feed(d, n); },
        [&](std::uint64_t received) {
            if (received - last_shown >= 512 * 1024 || received == manifest->tar_bytes) {
                last_shown = received;
                status("Downloading " + mb(received) + " of " + mb(manifest->tar_bytes) + "...");
            }
        });
    if (out != nullptr) {
        std::fclose(out);
    }
    if (tar.status != 200 || !verifier.complete()) {
        remove_tree(kStaging);
        result.message = "Not installed: " +
                         (!verifier.error().empty() ? verifier.error()
                                                    : explain(tar, "the download failed")) +
                         ". Nothing on the SD card was changed.";
        return result;
    }

    // 3. Plan: the previous seed's files go; another mod's files are kept unless asked.
    const std::string record_path = kSd + std::string(dreadovania::kSdManifest);
    auto owned = dreadovania::parse_sd_manifest(read_text(record_path));
    auto plan = dreadovania::plan_install(*manifest, owned,
                                          [](const std::string& p) { return exists(kSd + p); });
    if (!plan.conflicts.empty() && !overwrite) {
        result.conflicts = plan.conflicts;
        result.message = std::to_string(plan.conflicts.size()) +
                         " file(s) on the SD card belong to another mod and would be replaced, "
                         "e.g. " + plan.conflicts.front() + ". Press A again to replace them. "
                         "If they're from a seed you copied by hand, that's fine.";
        return result;  // staging is kept, but re-downloaded next time anyway
    }

    // 4. Install. Record first, so an interruption is cleaned up next time.
    status("Installing...");
    make_dirs(record_path.substr(0, record_path.rfind('/')));
    if (!write_text(record_path, dreadovania::sd_manifest_json(plan.record, seed_id, "installing"))) {
        remove_tree(kStaging);
        result.message = "Not installed: couldn't write to the SD card.";
        return result;
    }
    for (const std::string& path : plan.remove) {
        if (dreadovania::in_mod_roots(path)) {  // never outside Dread's folders
            std::remove((kSd + path).c_str());
        }
    }
    for (const std::string& dir : dreadovania::folders_to_tidy(plan.remove)) {
        rmdir((kSd + dir).c_str());  // fails, harmlessly, if another mod's files are there
    }
    for (const std::string& path : plan.files) {
        std::string dest = kSd + path;
        make_dirs(dest.substr(0, dest.rfind('/')));
        std::remove(dest.c_str());
        if (std::rename((kStaging + path).c_str(), dest.c_str()) != 0) {
            result.message = "Install interrupted: couldn't move " + path +
                             " into place. Install again to finish.";
            return result;
        }
    }
    write_text(record_path, dreadovania::sd_manifest_json(plan.files, seed_id, "installed"));
    remove_tree(kStaging);
    result.installed = true;
    result.message = "Installed (" + std::to_string(plan.files.size()) + " files, " +
                     mb(manifest->size_bytes) +
                     "). Close this app and start Dread with a new save file.";
    return result;
}

}  // namespace app
