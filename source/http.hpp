// SPDX-License-Identifier: GPL-3.0-or-later
// HTTPS to the server with libcurl, whose TLS on the Switch is the console's
// own ssl service (devkitPro builds curl with its libnx backend; ADR 0006).
// Certificates are always verified: against the Switch's trusted roots, plus
// the user's own CA from /config/dreadovania/ca.pem if they put one there.
// There is no option to skip verification.
#pragma once

#include <string>
#include <string_view>

#include "dreadovania/api.hpp"

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

    Response request(std::string_view method, const std::string& url,
                     const std::string& body = "", const std::string& token = "");

  private:
    bool ready_ = false;
    std::string ca_dir_;
    std::size_t user_ca_count_ = 0;
    dreadovania::UserCa user_ca_ = dreadovania::UserCa::Missing;
};

}  // namespace app
