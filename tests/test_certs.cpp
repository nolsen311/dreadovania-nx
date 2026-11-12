// SPDX-License-Identifier: GPL-3.0-or-later
// Client certificates for secure remote access (server ADR 0023).
#include <string>

#include "check.hpp"
#include "dreadovania/api.hpp"
#include "dreadovania/certs.hpp"
#include "dreadovania/servers.hpp"

using namespace dreadovania;

TEST(base64_decodes_with_and_without_padding) {
    CHECK(base64_decode("") == std::string());
    CHECK(base64_decode("TWFu") == std::string("Man"));
    CHECK(base64_decode("TWE=") == std::string("Ma"));
    CHECK(base64_decode("TWE") == std::string("Ma"));
    CHECK(base64_decode("TQ==") == std::string("M"));
    CHECK(base64_decode("AP8=") == std::string("\x00\xff", 2));
    CHECK(!base64_decode("TW F").has_value());  // not base64
    CHECK(!base64_decode("T").has_value());     // a lone character can't be
}

TEST(pairing_answer_with_a_certificate) {
    std::string body = R"({"device_id": "dev-1", "name": "Switch", "token": "dvt_x",
        "server_url": "https://dread.example.com",
        "certificate": {"p12": "AP8=", "password": "pw", "sha256": "ab",
                        "not_after": "2026-12-29T00:00:00+00:00",
                        "renew_after": "2026-12-13T08:00:00+00:00"}})";
    auto paired = parse_paired(body);
    CHECK(paired.has_value());
    CHECK(paired->server_url == "https://dread.example.com");
    CHECK(paired->certificate.has_value());
    CHECK(paired->certificate->p12 == std::string("\x00\xff", 2));
    CHECK(paired->certificate->password == "pw");
    CHECK(paired->certificate->renew_after == "2026-12-13T08:00:00+00:00");
}

TEST(pairing_answer_from_an_older_server) {
    auto paired = parse_paired(R"({"device_id": "d", "name": "S", "token": "dvt_x"})");
    CHECK(paired.has_value());
    CHECK(!paired->certificate.has_value());
    CHECK(paired->server_url.empty());
}

TEST(a_renewed_certificate_and_a_broken_one) {
    auto renewed = parse_client_certificate(R"({"p12": "TWFu", "password": "x",
        "sha256": "cd", "not_after": "n", "renew_after": "r"})");
    CHECK(renewed.has_value() && renewed->p12 == "Man" && renewed->sha256 == "cd");
    CHECK(!parse_client_certificate(R"({"p12": "!!", "password": "x"})").has_value());
    CHECK(!parse_client_certificate(R"({"password": "x"})").has_value());
    CHECK(!parse_client_certificate("not json").has_value());
}

TEST(device_me_says_when_to_renew) {
    auto status = parse_certificate_status(R"({"id": "d", "certificate": {"issuer": "cloudflare",
        "sha256": "ef", "not_after": "2027-01-01T00:00:00+00:00",
        "renew_after": "2026-11-12T00:00:00+00:00"}})");
    CHECK(status.has_value() && status->issuer == "cloudflare");
    CHECK(!parse_certificate_status(R"({"id": "d", "certificate": null})").has_value());
    CHECK(!parse_certificate_status(R"({"id": "d"})").has_value());  // an older server
}

TEST(renewal_is_due_from_renew_after_on) {
    const char* renew = "2026-12-13T08:00:00.123+00:00";
    CHECK(!renewal_due(renew, "2026-12-13T07:59:59"));
    CHECK(renewal_due(renew, "2026-12-13T08:00:00"));
    CHECK(renewal_due(renew, "2027-01-01 00:00:00"));  // a space instead of the T
    CHECK(!renewal_due("", "2027-01-01T00:00:00"));
    CHECK(!renewal_due("soon", "2027-01-01T00:00:00"));
    CHECK(!renewal_due(renew, ""));
}

TEST(certificate_files_have_safe_names) {
    CHECK(certificate_file("3f2a-11") == "/config/dreadovania/certificates/3f2a-11.p12");
    CHECK(certificate_file("../../evil") == "/config/dreadovania/certificates/.._.._evil.p12");
    auto own = own_certificate_files("https://dread.example.com:8443");
    CHECK(own.has_value());
    CHECK(own->p12 == "/config/dreadovania/certificates/dread.example.com_8443.p12");
    CHECK(own->password == "/config/dreadovania/certificates/dread.example.com_8443.password");
    CHECK(!own_certificate_files("http://plain").has_value());
    CHECK(!own_certificate_files("https://").has_value());
}

TEST(the_server_list_keeps_the_certificate) {
    ServerList list;
    PairedServer s;
    s.server = "https://dread.home";
    s.device_id = "dev";
    s.token = "dvt_x";
    s.cert_password = "pw \"quoted\"";
    s.cert_sha256 = "ab";
    s.cert_not_after = "2027-01-01T00:00:00+00:00";
    s.cert_renew_after = "2026-12-01T00:00:00+00:00";
    list.servers.push_back(s);
    auto back = parse_server_list(serialize_server_list(list));
    CHECK(back.has_value() && back->servers.size() == 1);
    CHECK(back->servers[0].cert_password == "pw \"quoted\"");
    CHECK(back->servers[0].cert_renew_after == "2026-12-01T00:00:00+00:00");
    list.servers[0].cert_password.clear();  // no certificate: none written
    CHECK(serialize_server_list(list).find("certificate") == std::string::npos);
}
