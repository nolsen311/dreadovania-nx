// SPDX-License-Identifier: GPL-3.0-or-later
#include "dreadovania/api.hpp"

#include <cctype>

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
    return Paired{*id, parsed.value->string_at("name").value_or(""), *token};
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
        }
        seeds.push_back(std::move(seed));
    }
    return seeds;
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

}  // namespace dreadovania
