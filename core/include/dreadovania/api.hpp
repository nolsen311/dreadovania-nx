// SPDX-License-Identifier: GPL-3.0-or-later
// What the app sends to and reads from the server (dreadovania-server
// docs/openapi.json, ADR 0017), and its settings file on the SD card.
#pragma once

#include <array>
#include <cstdint>
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
    std::string job_id;       // the job that generated it
    std::string mod_job_id;   // the job that patched it (or is patching it)
};
std::optional<std::vector<SeedSummary>> parse_seed_list(std::string_view body);

// The chip on a seed's row: its words and its colour family.
enum class Tone { Muted, Ok, Wait, Bad, Accent };
struct SeedState {
    std::string label;
    Tone tone;
};
SeedState seed_state(const SeedSummary& seed, bool installed);

// Dread's program ID: its mod folder is atmosphere/contents/010093801237c000.
inline constexpr std::uint64_t kDreadProgramId = 0x010093801237C000ULL;

// Why installing now would be unsafe, or nothing. The app runs either as the
// application (title takeover: no game runs alongside it) or as an applet
// (from the Album), where a suspended game may be running. If that game is
// Dread, its mod files are in use: replacing them could leave a half-installed
// seed or crash it when it resumes.
std::optional<std::string> install_blocker(bool app_is_application,
                                           std::optional<std::uint64_t> running_program);

// App updates (server ADR 0021): GET /app/latest.
struct AppLatest {
    bool available = false;
    std::string version;         // e.g. "0.4.3"
    std::uint64_t size = 0;
    std::string sha256;
    std::string minimum_version;  // the oldest app this server works with
    std::string reason;          // why nothing is available
};
std::optional<AppLatest> parse_app_latest(std::string_view body);

// "0.4.2" or "v0.4.2" -> {0, 4, 2}; nothing if it isn't a version.
std::optional<std::array<int, 3>> parse_version(std::string_view text);
// -1, 0 or 1; an unreadable version counts as older than any other.
int compare_versions(std::string_view a, std::string_view b);

enum class UpdateNeed { None, Available, Required };
// Whether this app (`current`) should update, given the server's answer.
UpdateNeed update_need(std::string_view current, const AppLatest& latest);

// "2026-11-09T20:00:00+00:00" -> "Nov 9"; "" if it isn't a date.
std::string short_date(std::string_view iso);

// A preset description as plain text: Randovania's <br/> become new lines,
// other tags are dropped and &amp; &lt; &gt; &quot; &#39; are decoded.
std::string plain_text(std::string_view html);

// One line for the seed list, at most `width` characters.
std::string describe_seed(const SeedSummary& seed, std::size_t width);

// Presets to generate from (GET /presets).
struct PresetSummary {
    std::string id;
    std::string name;
    std::string description;
};
std::optional<std::vector<PresetSummary>> parse_preset_list(std::string_view body);

// POST /seeds: a new seed from a preset; race seeds have no spoiler.
std::string generate_request_body(std::string_view preset_id, bool race);

// The answer to POST /seeds and POST /seeds/{id}/mod: the seed and its job.
struct Submitted {
    std::string seed_id;
    std::string job_id;
};
std::optional<Submitted> parse_submitted(std::string_view body);

// GET /jobs/{id}.
struct JobState {
    std::string status;    // queued, running, succeeded, failed, cancelled
    std::string progress;  // the server's latest progress line, may be empty
    std::string error;     // why it failed, may be empty
    bool finished() const { return status == "succeeded" || status == "failed" || status == "cancelled"; }
};
std::optional<JobState> parse_job(std::string_view body);

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
