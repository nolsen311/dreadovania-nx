// SPDX-License-Identifier: GPL-3.0-or-later
// HTTPS to the server with libcurl and mbedTLS (ADR 0004). Certificates are
// always verified: against the bundled Mozilla roots, plus the user's own CA
// from /config/dreadovania/ca.pem if they put one there. There is no option
// to skip verification.
#pragma once

#include <string>
#include <string_view>

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

    Response request(std::string_view method, const std::string& url,
                     const std::string& body = "", const std::string& token = "");

  private:
    bool ready_ = false;
    std::string ca_file_;
};

}  // namespace app
