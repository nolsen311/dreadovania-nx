// SPDX-License-Identifier: GPL-3.0-or-later
// A stand-in for source/http.hpp in the host tests: answers come from a map
// instead of the network, so source/installer.cpp runs unchanged on the host.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace app {

struct Response {
    long status = 0;
    std::string body;
    std::string error;
};

class Http {
  public:
    std::map<std::string, Response> routes;  // URL -> answer
    std::size_t chunk = 1000;
    std::vector<std::string> calls;  // "METHOD url body", in order

    Response request(std::string_view method, const std::string& url, const std::string& body = "",
                     const std::string& = "") {
        calls.push_back(std::string(method) + " " + url + (body.empty() ? "" : " " + body));
        auto it = routes.find(url);
        return it == routes.end() ? Response{404, "{\"detail\": \"not found\"}", ""} : it->second;
    }

    Response download(const std::string& url, const std::string& token,
                      const std::function<bool(const char*, std::size_t)>& on_data,
                      const std::function<void(std::uint64_t)>& on_progress) {
        Response r = request("GET", url, "", token);
        if (r.status != 200) {
            return r;
        }
        for (std::size_t i = 0; i < r.body.size(); i += chunk) {
            std::size_t n = std::min(chunk, r.body.size() - i);
            if (!on_data(r.body.data() + i, n)) {
                break;
            }
            on_progress(i + n);
        }
        r.body.clear();
        return r;
    }
};

}  // namespace app
