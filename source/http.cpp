// SPDX-License-Identifier: GPL-3.0-or-later
#include "http.hpp"

#include <curl/curl.h>
#include <switch.h>
#include <sys/stat.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "dreadovania/version.hpp"

namespace app {

namespace {

constexpr const char* kBundledRoots = "romfs:/cacert.pem";
constexpr const char* kUserCa = "sdmc:/config/dreadovania/ca.pem";
constexpr const char* kCombined = "sdmc:/config/dreadovania/trusted-roots.pem";
constexpr std::size_t kMaxBody = 4 * 1024 * 1024;

bool read_file(const char* path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

struct Sink {
    std::string* body;
    bool too_big = false;
};

size_t on_data(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<Sink*>(user);
    size_t n = size * count;
    if (sink->body->size() + n > kMaxBody) {
        sink->too_big = true;
        return 0;  // aborts the transfer
    }
    sink->body->append(data, n);
    return n;
}

std::string switch_time_utc() {
    std::time_t now = std::time(nullptr);
    std::tm utc{};
    if (gmtime_r(&now, &utc) == nullptr) {
        return "";
    }
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &utc);
    return text;
}

std::string describe(CURLcode code, const char* detail, dreadovania::UserCa ca) {
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
            return "the server's name couldn't be found: check the address, and that the "
                   "Switch uses your network's DNS (not 90DNS or another custom DNS)";
        case CURLE_COULDNT_CONNECT:
            return "the server didn't accept the connection: is it running, and is the "
                   "address right?";
        case CURLE_OPERATION_TIMEDOUT:
            return "the server took too long to answer";
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CACERT_BADFILE:
            return dreadovania::untrusted_certificate_message(detail, ca, switch_time_utc());
        case CURLE_SSL_CONNECT_ERROR:
            return std::string("the secure connection failed (") + detail +
                   "): the server must offer TLS 1.2 or newer";
        default:
            return std::string("the request failed (") +
                   (detail[0] != '\0' ? detail : curl_easy_strerror(code)) + ")";
    }
}

}  // namespace

std::string Http::init() {
    if (R_FAILED(socketInitializeDefault())) {
        return "networking couldn't start";
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        socketExit();
        return "networking couldn't start";
    }
    std::string roots;
    if (!read_file(kBundledRoots, roots) || roots.empty()) {
        shutdown();
        return "the app's trusted certificates are missing: reinstall the app";
    }
    std::string user_ca;
    bool has_user_ca = read_file(kUserCa, user_ca);
    user_ca_ = dreadovania::classify_user_ca(user_ca, has_user_ca);
    if (user_ca_ == dreadovania::UserCa::Loaded) {
        roots += "\n" + user_ca;
    }
    // libcurl 7.69 can only read trusted roots from a file, so combine them.
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/dreadovania", 0777);
    std::ofstream out(kCombined, std::ios::binary | std::ios::trunc);
    out << roots;
    out.close();
    if (!out) {
        shutdown();
        return "couldn't write to the SD card (/config/dreadovania)";
    }
    ca_file_ = kCombined;
    ready_ = true;
    return "";
}

void Http::shutdown() {
    curl_global_cleanup();
    socketExit();
    ready_ = false;
}

Response Http::request(std::string_view method, const std::string& url, const std::string& body,
                       const std::string& token) {
    Response response;
    if (!ready_) {
        response.error = "networking isn't ready";
        return response;
    }
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.error = "networking isn't ready";
        return response;
    }
    Sink sink{&response.body};
    std::string method_str(method);
    std::string agent = "Dreadovania-NX/" + std::string(dreadovania::kVersion);
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    if (!body.empty()) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }
    std::string auth;
    if (!token.empty()) {
        auth = "Authorization: Bearer " + token;
        headers = curl_slist_append(headers, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method_str.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);  // never plain HTTP
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file_.c_str());
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);  // the API never redirects
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    char detail[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    } else if (sink.too_big) {
        response.error = "the server's answer was too large";
    } else {
        response.error = describe(code, detail, user_ca_);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

}  // namespace app
