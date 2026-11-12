// SPDX-License-Identifier: GPL-3.0-or-later
// Client certificates for a server's secure remote access (server ADR 0023): what
// the server hands out at pairing and renewal, when to renew, and where the
// certificate files live on the SD card.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace dreadovania {

namespace json {
class Value;
}

// Standard base64 (RFC 4648), padding optional; nothing on any other character.
std::optional<std::string> base64_decode(std::string_view text);

// A certificate from the server: the .p12 itself and how to use it. The .p12 is
// kept in a file on the SD card; the rest in servers.json, next to the token.
struct ClientCertificate {
    std::string p12;          // the PKCS#12 file's bytes (decoded)
    std::string password;     // secret, like the token
    std::string sha256;       // the certificate's fingerprint, hex
    std::string not_after;    // ISO 8601, UTC
    std::string renew_after;  // ISO 8601, UTC: ask for a new one after this
};

// The answer to POST /api/v1/devices/me/certificate (or the "certificate" part of
// the pairing answer, given as its own JSON object text).
std::optional<ClientCertificate> parse_client_certificate(std::string_view body);
std::optional<ClientCertificate> client_certificate_from(const json::Value& object);

// From GET /api/v1/devices/me: when to renew this Switch's certificate. Nothing if
// the server gave none (it hasn't issued one, or it's an older server).
struct CertificateStatus {
    std::string issuer;       // builtin, cloudflare or external
    std::string sha256;
    std::string not_after;
    std::string renew_after;
};
std::optional<CertificateStatus> parse_certificate_status(std::string_view body);

// Whether it's time to renew: ``now`` and ``renew_after`` are ISO 8601 UTC times,
// compared to the second. An empty or malformed time is never due.
bool renewal_due(std::string_view renew_after, std::string_view now);

// The file a server's certificate is kept in: one per paired device.
std::string certificate_file(std::string_view device_id);

// A certificate from the owner's own CA (server ADR 0023, the External issuer),
// put on the SD card by hand: <host>.p12, and its password in <host>.password,
// in the certificates folder. It's used for that server instead of the server's.
struct OwnCertificateFiles {
    std::string p12;
    std::string password;
};
std::optional<OwnCertificateFiles> own_certificate_files(std::string_view origin);

}  // namespace dreadovania
