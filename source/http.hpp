// SPDX-License-Identifier: GPL-3.0-or-later
// HTTPS to the server with libcurl, whose TLS on the Switch is the console's
// own ssl service (devkitPro builds curl with its libnx backend; ADR 0006).
// Certificates are always verified: against the Switch's trusted roots, plus
// the user's own CA from /config/dreadovania/ca.pem if they put one there.
// There is no option to skip verification.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "dreadovania/api.hpp"

struct curl_slist;  // from curl.h

namespace app {

struct Response {
    long status = 0;      // 0 when there was no HTTP response at all
    std::string body;
    std::string error;    // why there was no response, for the user
};

class Http {
  public:
    // Prepares networking and the trusted roots. Returns an error, or "".
    std::string init();
    void shutdown();
    dreadovania::UserCa user_ca() const { return user_ca_; }

    // Present this client certificate (a PKCS#12 file and its password) on every
    // connection from now on, or none with an empty path (server ADR 0023). A change
    // starts a fresh connection, so one server's certificate never goes to another.
    void use_client_certificate(const std::string& path, const std::string& password);
    bool has_client_certificate() const { return !cert_path_.empty(); }

    Response request(std::string_view method, const std::string& url,
                     const std::string& body = "", const std::string& token = "");

    // Streams a GET to on_data (return false to stop). On success status is 200
    // and body is empty; otherwise body holds the server's error answer.
    Response download(const std::string& url, const std::string& token,
                      const std::function<bool(const char*, std::size_t)>& on_data,
                      const std::function<void(std::uint64_t)>& on_progress);

    // How many of Mozilla's roots are ready as a second step (ADRs 0013, 0014), or 0.
    std::size_t public_root_count() const { return public_root_count_; }

  private:
    void prepare_public_roots(const std::vector<std::string>& user_certs);
    // One way of trying a server whose certificate wasn't trusted (ADRs 0014, 0015).
    struct Attempt {
        const std::string* folder;  // CURLOPT_CAPATH
        bool rsa;                   // offer only an RSA cipher suite
    };
    void plan_attempts();
    const Attempt* attempt_for(const std::string& url) const;
    bool with_public_roots(const std::string& url) const;
    bool next_attempt(const std::string& url);
    bool try_next(int code, const std::string& url);  // code: a CURLcode
    void setup(const std::string& url, const char* method, ::curl_slist* headers,
               char* detail);
    static ::curl_slist* headers(const std::string& token, bool json_body);

    void* handle_ = nullptr;  // CURL*, kept between requests to reuse the connection
    std::string agent_;
    bool ready_ = false;
    std::string ca_dir_;
    std::string cert_path_;
    std::string cert_password_;  // secret: never shown or logged
    std::size_t user_ca_count_ = 0;
    std::vector<std::string> public_ca_dirs_;  // one per batch; empty without the bundle
    std::size_t public_root_count_ = 0;
    std::vector<Attempt> attempts_;                    // in order; folders point into the above
    std::map<std::string, std::size_t> attempt_of_;   // server origin -> attempt it needed
    dreadovania::UserCa user_ca_ = dreadovania::UserCa::Missing;
};

}  // namespace app
