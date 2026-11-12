// SPDX-License-Identifier: GPL-3.0-or-later
#include "dreadovania/api.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>

#include "dreadovania/json.hpp"

namespace dreadovania {

namespace {

constexpr std::string_view kCodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  // server's

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool valid_host_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '.';
}

}  // namespace

std::optional<DeviceConfig> parse_device_config(std::string_view text) {
    json::ParseResult parsed = json::parse(text);
    if (!parsed.value || !parsed.value->is_object()) {
        return std::nullopt;
    }
    const json::Value& v = *parsed.value;
    DeviceConfig config;
    auto server = v.string_at("server");
    auto token = v.string_at("token");
    if (!server || !token || token->empty()) {
        return std::nullopt;
    }
    ServerUrl url = normalize_server_url(*server);
    if (!url.origin) {
        return std::nullopt;
    }
    config.server = *url.origin;
    config.token = *token;
    config.device_id = v.string_at("device_id").value_or("");
    config.name = v.string_at("name").value_or("");
    return config;
}

std::string serialize_device_config(const DeviceConfig& c) {
    return "{\n  \"server\": " + json::quote(c.server) + ",\n  \"device_id\": " +
           json::quote(c.device_id) + ",\n  \"name\": " + json::quote(c.name) +
           ",\n  \"token\": " + json::quote(c.token) + "\n}\n";
}

ServerUrl normalize_server_url(std::string_view input) {
    ServerUrl result;
    std::string_view s = input;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    std::string text = lower(s);
    if (text.rfind("http://", 0) == 0) {
        result.error = "the server must use https://: see the server's docs/https.md";
        return result;
    }
    if (text.rfind("https://", 0) == 0) {
        text.erase(0, 8);
    } else if (text.find("://") != std::string::npos) {
        result.error = "the address must start with https://";
        return result;
    }
    while (!text.empty() && text.back() == '/') {
        text.pop_back();
    }
    if (text.find_first_of("/?#@") != std::string::npos) {
        result.error = "enter just the server's address, like https://dread.example.com";
        return result;
    }
    std::string host = text;
    std::string port;
    if (std::size_t colon = text.rfind(':'); colon != std::string::npos) {
        host = text.substr(0, colon);
        port = text.substr(colon + 1);
        if (port.empty() || port.size() > 5) {
            result.error = "the port must be a number";
            return result;
        }
        for (char c : port) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                result.error = "the port must be a number";
                return result;
            }
        }
        int number = std::stoi(port);
        if (number < 1 || number > 65535) {
            result.error = "the port must be from 1 to 65535";
            return result;
        }
        if (number == 443) {
            port.clear();
        }
    }
    if (host.empty() || host.size() > 253 || host.front() == '.' || host.front() == '-' ||
        host.find("..") != std::string::npos) {
        result.error = "that isn't a server address";
        return result;
    }
    for (char c : host) {
        if (!valid_host_char(c)) {
            result.error = "that isn't a server address";
            return result;
        }
    }
    result.origin = "https://" + host + (port.empty() ? "" : ":" + port);
    return result;
}

std::optional<std::string> normalize_pairing_code(std::string_view input) {
    std::string code;
    for (char c : input) {
        if (c == '-' || std::isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (kCodeAlphabet.find(c) == std::string_view::npos) {
            return std::nullopt;
        }
        code += c;
    }
    if (code.size() != 8) {
        return std::nullopt;
    }
    return code;
}

std::string pair_request_body(std::string_view code, std::string_view name) {
    return "{\"code\": " + json::quote(code) + ", \"name\": " + json::quote(name) + "}";
}

std::optional<Paired> parse_paired(std::string_view body) {
    json::ParseResult parsed = json::parse(body);
    if (!parsed.value || !parsed.value->is_object()) {
        return std::nullopt;
    }
    auto id = parsed.value->string_at("device_id");
    auto token = parsed.value->string_at("token");
    if (!id || !token || token->empty()) {
        return std::nullopt;
    }
    Paired paired{*id, parsed.value->string_at("name").value_or(""), *token, std::nullopt,
                  parsed.value->string_at("server_url").value_or("")};
    if (const json::Value* cert = parsed.value->get("certificate"); cert && cert->is_object()) {
        paired.certificate = client_certificate_from(*cert);
    }
    return paired;
}

std::optional<std::vector<SeedSummary>> parse_seed_list(std::string_view body) {
    json::ParseResult parsed = json::parse(body);
    if (!parsed.value || !parsed.value->is_array()) {
        return std::nullopt;
    }
    std::vector<SeedSummary> seeds;
    for (const json::Value& item : parsed.value->items()) {
        if (!item.is_object()) {
            return std::nullopt;
        }
        auto id = item.string_at("id");
        auto status = item.string_at("status");
        if (!id || !status) {
            return std::nullopt;
        }
        SeedSummary seed;
        seed.id = *id;
        seed.status = *status;
        seed.label = item.string_at("preset_name").value_or("");
        if (seed.label.empty()) {
            seed.label = item.string_at("source").value_or("") == "permalink" ? "Permalink"
                                                                               : "Seed";
        }
        seed.created_at = item.string_at("created_at").value_or("");
        seed.seed_hash = item.string_at("seed_hash").value_or("");
        if (const json::Value* mod = item.get("mod"); mod != nullptr && mod->is_object()) {
            seed.mod_status = mod->string_at("status").value_or("");
            seed.mod_job_id = mod->string_at("job_id").value_or("");
        }
        seed.job_id = item.string_at("job_id").value_or("");
        seeds.push_back(std::move(seed));
    }
    return seeds;
}

std::optional<std::vector<PresetSummary>> parse_preset_list(std::string_view body) {
    json::ParseResult parsed = json::parse(body);
    if (!parsed.value || !parsed.value->is_array()) {
        return std::nullopt;
    }
    std::vector<PresetSummary> presets;
    for (const json::Value& item : parsed.value->items()) {
        auto id = item.is_object() ? item.string_at("id") : std::nullopt;
        if (!id || id->empty()) {
            return std::nullopt;
        }
        PresetSummary preset{*id, item.string_at("name").value_or(""),
                             item.string_at("description").value_or("")};
        if (preset.name.empty()) {
            preset.name = preset.id;
        }
        presets.push_back(std::move(preset));
    }
    return presets;
}

std::string generate_request_body(std::string_view preset_id, bool race) {
    return "{\"preset_id\": " + json::quote(preset_id) +
           ", \"race\": " + (race ? "true" : "false") + "}";
}

std::optional<Submitted> parse_submitted(std::string_view body) {
    json::ParseResult parsed = json::parse(body);
    if (!parsed.value || !parsed.value->is_object()) {
        return std::nullopt;
    }
    auto seed_id = parsed.value->string_at("seed_id");
    auto job_id = parsed.value->string_at("job_id");
    if (!seed_id || !job_id || seed_id->empty() || job_id->empty()) {
        return std::nullopt;
    }
    return Submitted{*seed_id, *job_id};
}

std::optional<JobState> parse_job(std::string_view body) {
    json::ParseResult parsed = json::parse(body);
    if (!parsed.value || !parsed.value->is_object()) {
        return std::nullopt;
    }
    auto status = parsed.value->string_at("status");
    if (!status || status->empty()) {
        return std::nullopt;
    }
    return JobState{*status, parsed.value->string_at("progress").value_or(""),
                    parsed.value->string_at("error").value_or("")};
}

std::string describe_seed(const SeedSummary& seed, std::size_t width) {
    std::string state;
    if (seed.status != "ready") {
        state = seed.status;
    } else if (seed.mod_status == "ready") {
        state = "patched";
    } else if (seed.mod_status == "pending") {
        state = "patching";
    } else if (seed.mod_status == "failed") {
        state = "patch failed";
    } else {
        state = "not patched";
    }
    std::string date = seed.created_at.substr(0, 10);
    std::string line = date + "  " + seed.label;
    if (!seed.seed_hash.empty()) {
        line += "  " + seed.seed_hash;
    }
    std::string suffix = "  [" + state + "]";
    if (line.size() + suffix.size() > width) {
        std::size_t keep = width > suffix.size() + 3 ? width - suffix.size() - 3 : 0;
        line = line.substr(0, keep) + "...";
    }
    std::string out = line + suffix;
    return out.size() > width ? out.substr(0, width) : out;
}

std::vector<std::string> split_pem_certificates(std::string_view text) {
    constexpr std::string_view kBegin = "-----BEGIN CERTIFICATE-----";
    constexpr std::string_view kEnd = "-----END CERTIFICATE-----";
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (true) {
        std::size_t begin = text.find(kBegin, pos);
        if (begin == std::string_view::npos) {
            break;
        }
        std::size_t end = text.find(kEnd, begin + kBegin.size());
        if (end == std::string_view::npos) {
            break;  // a truncated block is ignored
        }
        end += kEnd.size();
        out.emplace_back(std::string(text.substr(begin, end - begin)) + "\n");
        pos = end;
    }
    return out;
}

UserCa classify_user_ca(std::string_view file_contents, bool exists) {
    if (!exists) {
        return UserCa::Missing;
    }
    if (file_contents.find("-----BEGIN CERTIFICATE-----") == std::string_view::npos) {
        return UserCa::NotPem;
    }
    return UserCa::Loaded;
}

std::string untrusted_certificate_message(std::string_view tls_detail, UserCa ca,
                                          std::string_view switch_time_utc) {
    std::string out = "the server's certificate isn't trusted";
    if (!tls_detail.empty()) {
        out += " (" + std::string(tls_detail) + ")";
    }
    out += ". ";
    switch (ca) {
        case UserCa::Loaded:
            out += "Your CA from /config/dreadovania/ca.pem was used. ";
            break;
        case UserCa::NotPem:
            out += "/config/dreadovania/ca.pem isn't a PEM certificate (it must contain "
                   "\"BEGIN CERTIFICATE\"), so it was ignored. ";
            break;
        case UserCa::Missing:
            out += "If the server uses your own CA, copy its certificate to "
                   "/config/dreadovania/ca.pem. ";
            break;
    }
    out += "Also check the Switch's date and time";
    if (!switch_time_utc.empty()) {
        out += " (it says " + std::string(switch_time_utc) + " UTC)";
    }
    out += ": a new certificate looks \"not yet valid\" to a clock that is behind";
    return out;
}

std::string client_certificate_message(std::string_view tls_detail, bool has_certificate) {
    std::string why = tls_detail.empty() ? "" : " (" + std::string(tls_detail) + ")";
    if (has_certificate) {
        return "the server didn't accept this Switch's client certificate" + why +
               ": it may have been revoked, or have expired. Pair this Switch again";
    }
    return "the server asked for a client certificate, and this Switch doesn't have "
           "one for it yet" +
           why +
           ". Pair through an address that doesn't ask for one: behind Cloudflare, "
           "a hostname that isn't in its client-certificate (mTLS) list. The server's "
           "Secure remote access guide explains how";
}

std::string attempts_summary(const std::vector<std::pair<std::string, std::string>>& tried) {
    if (tried.size() < 2) {
        return "";
    }
    bool same = true;
    for (const auto& t : tried) {
        same = same && t.second == tried.front().second;
    }
    std::string out = ". Tried: ";
    for (std::size_t i = 0; i < tried.size(); ++i) {
        out += (i ? "; " : "") + tried[i].first;
        if (!same) {
            out += ": " + tried[i].second;
        }
    }
    if (same) {
        out += ". All: " + tried.front().second;
    }
    return out;
}

std::optional<std::string> problem_detail(std::string_view body) {
    json::ParseResult parsed = json::parse(body);
    if (!parsed.value || !parsed.value->is_object()) {
        return std::nullopt;
    }
    if (auto detail = parsed.value->string_at("detail"); detail && !detail->empty()) {
        return detail;
    }
    return parsed.value->string_at("title");
}

SeedState seed_state(const SeedSummary& seed, bool installed) {
    if (installed) {
        return {"Installed", Tone::Accent};
    }
    if (seed.status == "pending") {
        return {"Generating", Tone::Wait};
    }
    if (seed.status == "failed" || seed.status == "cancelled") {
        return {seed.status == "failed" ? "Failed" : "Cancelled", Tone::Bad};
    }
    if (seed.mod_status == "ready") {
        return {"Patched", Tone::Ok};
    }
    if (seed.mod_status == "pending") {
        return {"Patching", Tone::Wait};
    }
    if (seed.mod_status == "failed") {
        return {"Patch failed", Tone::Bad};
    }
    return {"Not patched", Tone::Muted};
}

std::string short_date(std::string_view iso) {
    static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') {
        return "";
    }
    auto digits = [&](std::size_t at) {
        return std::isdigit(static_cast<unsigned char>(iso[at])) &&
               std::isdigit(static_cast<unsigned char>(iso[at + 1]));
    };
    if (!digits(5) || !digits(8)) {
        return "";
    }
    int month = (iso[5] - '0') * 10 + (iso[6] - '0');
    int day = (iso[8] - '0') * 10 + (iso[9] - '0');
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return "";
    }
    return std::string(kMonths[month - 1]) + " " + std::to_string(day);
}

std::string plain_text(std::string_view html) {
    std::string out;
    for (std::size_t i = 0; i < html.size();) {
        if (html[i] == '<') {
            std::size_t end = html.find('>', i);
            if (end == std::string_view::npos) {
                break;
            }
            std::string tag = lower(html.substr(i + 1, end - i - 1));
            if (tag.rfind("br", 0) == 0 || tag == "/p") {
                out += '\n';
            }
            i = end + 1;
            continue;
        }
        if (html[i] == '&') {
            static constexpr std::pair<std::string_view, char> kEntities[] = {
                {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&#39;", '\''}};
            bool done = false;
            for (const auto& [name, ch] : kEntities) {
                if (html.substr(i, name.size()) == name) {
                    out += ch;
                    i += name.size();
                    done = true;
                    break;
                }
            }
            if (done) {
                continue;
            }
        }
        out += html[i++];
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

std::optional<std::string> install_blocker(bool app_is_application,
                                           std::optional<std::uint64_t> running_program) {
    if (app_is_application || running_program != kDreadProgramId) {
        return std::nullopt;
    }
    return "Dread is running in the background. Close it first (press HOME, then X on "
           "Dread), then install. Nothing on the SD card was changed.";
}

std::optional<AppLatest> parse_app_latest(std::string_view body) {
    json::ParseResult parsed = json::parse(body);
    if (!parsed.value || !parsed.value->is_object()) {
        return std::nullopt;
    }
    const json::Value& v = *parsed.value;
    AppLatest out;
    out.available = v.bool_at("available").value_or(false);
    out.minimum_version = v.string_at("minimum_version").value_or("");
    out.reason = v.string_at("reason").value_or("");
    if (out.available) {
        auto version = v.string_at("version");
        auto sha = v.string_at("sha256");
        auto size = v.int_at("size");
        if (!version || !parse_version(*version) || !sha || sha->size() != 64 || !size ||
            *size <= 0) {
            return std::nullopt;
        }
        out.version = *version;
        out.sha256 = *sha;
        out.size = static_cast<std::uint64_t>(*size);
    }
    return out;
}

std::optional<std::array<int, 3>> parse_version(std::string_view text) {
    if (!text.empty() && text.front() == 'v') {
        text.remove_prefix(1);
    }
    std::array<int, 3> parts{};
    std::size_t part = 0;
    bool digit = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            if (parts[part] > 99999) {
                return std::nullopt;
            }
            parts[part] = parts[part] * 10 + (c - '0');
            digit = true;
        } else if (c == '.' && digit && part < 2) {
            ++part;
            digit = false;
        } else {
            return std::nullopt;
        }
    }
    if (part != 2 || !digit) {
        return std::nullopt;
    }
    return parts;
}

int compare_versions(std::string_view a, std::string_view b) {
    auto va = parse_version(a);
    auto vb = parse_version(b);
    if (!va || !vb) {
        return va ? 1 : vb ? -1 : 0;
    }
    return *va < *vb ? -1 : *va > *vb ? 1 : 0;
}

UpdateNeed update_need(std::string_view current, const AppLatest& latest) {
    if (!latest.minimum_version.empty() && compare_versions(current, latest.minimum_version) < 0) {
        return UpdateNeed::Required;
    }
    if (latest.available && compare_versions(latest.version, current) > 0) {
        return UpdateNeed::Available;
    }
    return UpdateNeed::None;
}

std::string origin_of(std::string_view url) {
    constexpr std::string_view kScheme = "https://";
    if (url.size() <= kScheme.size()) {
        return "";
    }
    std::string lower;
    for (char c : url.substr(0, kScheme.size())) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower != kScheme) {
        return "";
    }
    std::string_view rest = url.substr(kScheme.size());
    std::size_t end = rest.find_first_of("/?#");
    std::string_view authority = rest.substr(0, end);
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return "";
    }
    std::string out(kScheme);
    for (char c : authority) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace dreadovania
