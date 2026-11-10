// SPDX-License-Identifier: GPL-3.0-or-later
// What the app sends to and reads from the server (dreadovania-server
// docs/openapi.json, ADR 0017), and its settings file on the SD card.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dreadovania {

// The settings file: which server, and this device's token (ADR 0004).
inline constexpr std::string_view kDeviceFile = "/config/dreadovania/device.json";

struct DeviceConfig {
    std::string server;     // an origin, e.g. https://dread.home.example.com
    std::string device_id;
    std::string name;
    std::string token;      // secret: never shown or logged
};

std::optional<DeviceConfig> parse_device_config(std::string_view text);
std::string serialize_device_config(const DeviceConfig& config);

// "dread.example.com", "https://dread.example.com/", ... -> "https://dread.example.com".
// Only HTTPS: the app never talks to the server without verified TLS.
struct ServerUrl {
    std::optional<std::string> origin;
    std::string error;
};
ServerUrl normalize_server_url(std::string_view input);

// "abcd-efgh", "ABCD EFGH" -> "ABCDEFGH"; nothing if it can't be a code.
std::optional<std::string> normalize_pairing_code(std::string_view input);

std::string pair_request_body(std::string_view code, std::string_view name);

struct Paired {
    std::string device_id;
    std::string name;
    std::string token;
};
std::optional<Paired> parse_paired(std::string_view body);

struct SeedSummary {
    std::string id;
    std::string status;       // pending, ready, failed, cancelled
    std::string label;        // preset name, or "Permalink"
    std::string created_at;   // ISO 8601
    std::string seed_hash;    // the in-game hash, if ready
    std::string mod_status;   // "" if not patched, else pending/ready/failed
};
std::optional<std::vector<SeedSummary>> parse_seed_list(std::string_view body);

// One line for the seed list, at most `width` characters.
std::string describe_seed(const SeedSummary& seed, std::size_t width);

// Whether the user's own CA (/config/dreadovania/ca.pem) was added to the trusted roots.
enum class UserCa { Missing, Loaded, NotPem };
UserCa classify_user_ca(std::string_view file_contents, bool exists);

// Each "-----BEGIN CERTIFICATE----- ... -----END CERTIFICATE-----" block, in
// order, as its own PEM text. The Switch's TLS service imports one certificate
// per call, so a file with several must be split (ADR 0006).
std::vector<std::string> split_pem_certificates(std::string_view text);

// Why a certificate wasn't trusted, with what to check. `tls_detail` is the TLS
// library's own reason (may be empty); `switch_time_utc` is the console's clock.
std::string untrusted_certificate_message(std::string_view tls_detail, UserCa ca,
                                          std::string_view switch_time_utc);

// The "detail" (or "title") of an RFC 9457 problem response, if there is one.
std::optional<std::string> problem_detail(std::string_view body);

}  // namespace dreadovania
