// SPDX-License-Identifier: GPL-3.0-or-later
#include "dreadovania/certs.hpp"

#include <array>
#include <cctype>

#include "dreadovania/json.hpp"

namespace dreadovania {

namespace {

constexpr std::string_view kCertDir = "/config/dreadovania/certificates/";

int value_of(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Letters, digits, '.', '-' and '_' only: a safe file name for any input.
std::string file_safe(std::string_view text) {
    std::string out;
    for (char c : text) {
        unsigned char u = static_cast<unsigned char>(c);
        out += (std::isalnum(u) != 0 || c == '.' || c == '-' || c == '_') ? c : '_';
    }
    return out;
}

}  // namespace

std::optional<ClientCertificate> client_certificate_from(const json::Value& v) {
    if (!v.is_object()) {
        return std::nullopt;
    }
    auto p12 = v.string_at("p12");
    auto password = v.string_at("password");
    if (!p12 || !password) {
        return std::nullopt;
    }
    auto bytes = base64_decode(*p12);
    if (!bytes || bytes->empty()) {
        return std::nullopt;
    }
    return ClientCertificate{*bytes, *password, v.string_at("sha256").value_or(""),
                             v.string_at("not_after").value_or(""),
                             v.string_at("renew_after").value_or("")};
}

std::optional<std::string> base64_decode(std::string_view text) {
    while (!text.empty() && text.back() == '=') {
        text.remove_suffix(1);
    }
    std::string out;
    out.reserve(text.size() * 3 / 4);
    unsigned int buffer = 0;
    int bits = 0;
    for (char c : text) {
        int v = value_of(c);
        if (v < 0) {
            return std::nullopt;
        }
        buffer = (buffer << 6) | static_cast<unsigned int>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xFFu);
        }
    }
    if (bits >= 6) {  // a lone leftover character can't be valid base64
        return std::nullopt;
    }
    return out;
}

std::optional<ClientCertificate> parse_client_certificate(std::string_view body) {
    json::ParseResult parsed = json::parse(body);
    if (!parsed.value) {
        return std::nullopt;
    }
    return client_certificate_from(*parsed.value);
}

std::optional<CertificateStatus> parse_certificate_status(std::string_view body) {
    json::ParseResult parsed = json::parse(body);
    if (!parsed.value || !parsed.value->is_object()) {
        return std::nullopt;
    }
    const json::Value* cert = parsed.value->get("certificate");
    if (cert == nullptr || !cert->is_object()) {
        return std::nullopt;
    }
    return CertificateStatus{cert->string_at("issuer").value_or(""),
                             cert->string_at("sha256").value_or(""),
                             cert->string_at("not_after").value_or(""),
                             cert->string_at("renew_after").value_or("")};
}

bool renewal_due(std::string_view renew_after, std::string_view now) {
    // "2026-11-12T04:37:11" is 19 characters; both come from UTC clocks.
    constexpr std::size_t kSeconds = 19;
    auto well_formed = [](std::string_view t) {
        return t.size() >= kSeconds && t[4] == '-' && t[7] == '-' &&
               (t[10] == 'T' || t[10] == ' ') && t[13] == ':' && t[16] == ':';
    };
    if (!well_formed(renew_after) || !well_formed(now)) {
        return false;
    }
    std::string a(renew_after.substr(0, kSeconds)), b(now.substr(0, kSeconds));
    a[10] = b[10] = 'T';
    return b >= a;
}

std::string certificate_file(std::string_view device_id) {
    return std::string(kCertDir) + file_safe(device_id) + ".p12";
}

std::optional<OwnCertificateFiles> own_certificate_files(std::string_view origin) {
    std::string_view rest = origin;
    if (rest.substr(0, 8) == "https://") {
        rest.remove_prefix(8);
    } else {
        return std::nullopt;
    }
    if (std::size_t slash = rest.find('/'); slash != std::string_view::npos) {
        rest = rest.substr(0, slash);
    }
    if (rest.empty()) {
        return std::nullopt;
    }
    std::string host = file_safe(rest);  // dread.example.com:8443 -> dread.example.com_8443
    std::string base = std::string(kCertDir) + host;
    return OwnCertificateFiles{base + ".p12", base + ".password"};
}

}  // namespace dreadovania
