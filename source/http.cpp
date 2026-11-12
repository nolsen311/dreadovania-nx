// SPDX-License-Identifier: GPL-3.0-or-later
#include "http.hpp"

#include <curl/curl.h>
#include <switch.h>
#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "dreadovania/version.hpp"

namespace app {

namespace {

constexpr const char* kUserCa = "sdmc:/config/dreadovania/ca.pem";
// The user's CA certificates, one per file: the TLS service imports one
// certificate per call, and curl's libnx backend imports each file in
// CURLOPT_CAPATH separately (ADR 0006).
constexpr const char* kUserCaDir = "sdmc:/config/dreadovania/trusted";
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

std::string describe(CURLcode code, const char* detail, dreadovania::UserCa ca,
                     bool has_certificate) {
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
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CACERT_BADFILE:
            return dreadovania::untrusted_certificate_message(detail, ca, switch_time_utc());
        case CURLE_SSL_CONNECT_ERROR:
            return std::string("the secure connection failed (") + detail +
                   (has_certificate
                        ? "): the server must offer TLS 1.2 or newer, and accept this "
                          "Switch's certificate (if it was revoked, pair again)"
                        : "): the server must offer TLS 1.2 or newer. If it only lets in "
                          "devices with a certificate, pair this Switch again");
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
    // Public authorities are checked against the Switch's own trusted roots.
    // The user's own CA is added from ca.pem, one certificate per file.
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/dreadovania", 0777);
    mkdir(kUserCaDir, 0777);
    if (DIR* dir = opendir(kUserCaDir)) {  // forget certificates from an older ca.pem
        while (dirent* entry = readdir(dir)) {
            if (entry->d_name[0] != '.') {
                std::remove((std::string(kUserCaDir) + "/" + entry->d_name).c_str());
            }
        }
        closedir(dir);
    }
    std::string user_ca;
    bool has_user_ca = read_file(kUserCa, user_ca);
    user_ca_ = dreadovania::classify_user_ca(user_ca, has_user_ca);
    if (user_ca_ == dreadovania::UserCa::Loaded) {
        std::vector<std::string> certs = dreadovania::split_pem_certificates(user_ca);
        for (std::size_t i = 0; i < certs.size(); ++i) {
            std::string path = std::string(kUserCaDir) + "/ca-" + std::to_string(i) + ".pem";
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << certs[i];
            if (!out) {
                shutdown();
                return "couldn't write to the SD card (/config/dreadovania)";
            }
        }
        user_ca_count_ = certs.size();
        ca_dir_ = kUserCaDir;
    }
    agent_ = "Dreadovania-NX/" + std::string(dreadovania::kVersion);
    ready_ = true;
    return "";
}

void Http::shutdown() {
    if (handle_ != nullptr) {
        curl_easy_cleanup(handle_);
        handle_ = nullptr;
    }
    curl_global_cleanup();
    socketExit();
    ready_ = false;
}

void Http::use_client_certificate(const std::string& path, const std::string& password) {
    if (path == cert_path_ && password == cert_password_) {
        return;
    }
    cert_path_ = path;
    cert_password_ = password;
    if (handle_ != nullptr) {  // drop open connections made with the other certificate
        curl_easy_cleanup(handle_);
        handle_ = nullptr;
    }
}

void Http::setup(const std::string& url, const char* method, curl_slist* headers, char* detail) {
    // One handle for the whole session: curl keeps the connection open between
    // requests, which saves a TLS handshake each time (ADR 0018).
    if (handle_ == nullptr) {
        handle_ = curl_easy_init();
    } else {
        curl_easy_reset(handle_);
    }
    CURL* curl = handle_;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, agent_.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);  // never plain HTTP
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    // No CA file: the Switch's trusted roots, plus the user's CA if there is one.
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);
    if (!ca_dir_.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAPATH, ca_dir_.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    if (!cert_path_.empty()) {
        // libnx's TLS backend loads the identity from a PKCS#12 file into the
        // Switch's ssl service (sslContextImportClientPki).
        curl_easy_setopt(curl, CURLOPT_SSLCERT, cert_path_.c_str());
        curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "P12");
        curl_easy_setopt(curl, CURLOPT_KEYPASSWD, cert_password_.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);  // the API never redirects
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
}

curl_slist* Http::headers(const std::string& token, bool json_body) {
    curl_slist* list = curl_slist_append(nullptr, "Accept: application/json, application/x-tar");
    if (json_body) {
        list = curl_slist_append(list, "Content-Type: application/json");
    }
    if (!token.empty()) {
        list = curl_slist_append(list, ("Authorization: Bearer " + token).c_str());
    }
    return list;
}

Response Http::request(std::string_view method, const std::string& url, const std::string& body,
                       const std::string& token) {
    Response response;
    if (!ready_) {
        response.error = "networking isn't ready";
        return response;
    }
    Sink sink{&response.body};
    std::string method_str(method);
    curl_slist* list = headers(token, !body.empty());
    char detail[CURL_ERROR_SIZE] = {};
    setup(url, method_str.c_str(), list, detail);
    curl_easy_setopt(handle_, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(handle_, CURLOPT_WRITEDATA, &sink);
    if (!body.empty()) {
        curl_easy_setopt(handle_, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(handle_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    CURLcode code = curl_easy_perform(handle_);
    if (code == CURLE_OK) {
        curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &response.status);
    } else if (sink.too_big) {
        response.error = "the server's answer was too large";
    } else {
        response.error = describe(code, detail, user_ca_, !cert_path_.empty());
    }
    curl_slist_free_all(list);
    return response;
}

namespace {

struct Stream {
    const std::function<bool(const char*, std::size_t)>* on_data;
    const std::function<void(std::uint64_t)>* on_progress;
    std::uint64_t received = 0;
    long status = 0;
    CURL* curl = nullptr;
    bool stopped = false;
    std::string error_body;  // a problem response is JSON, not the tar
};

size_t on_stream(char* data, size_t size, size_t count, void* user) {
    auto* s = static_cast<Stream*>(user);
    size_t n = size * count;
    if (s->status == 0) {
        curl_easy_getinfo(s->curl, CURLINFO_RESPONSE_CODE, &s->status);
    }
    if (s->status != 200) {
        if (s->error_body.size() < 64 * 1024) {
            s->error_body.append(data, n);
        }
        return n;
    }
    if (!(*s->on_data)(data, n)) {
        s->stopped = true;
        return 0;
    }
    s->received += n;
    if (*s->on_progress) {
        (*s->on_progress)(s->received);
    }
    return n;
}

}  // namespace

Response Http::download(const std::string& url, const std::string& token,
                        const std::function<bool(const char*, std::size_t)>& on_data,
                        const std::function<void(std::uint64_t)>& on_progress) {
    Response response;
    if (!ready_) {
        response.error = "networking isn't ready";
        return response;
    }
    curl_slist* list = headers(token, false);
    char detail[CURL_ERROR_SIZE] = {};
    setup(url, "GET", list, detail);
    Stream stream{&on_data, &on_progress, 0, 0, handle_, false, {}};
    // No overall time limit for a download; give up if it stalls for 30 seconds.
    curl_easy_setopt(handle_, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(handle_, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, on_stream);
    curl_easy_setopt(handle_, CURLOPT_WRITEDATA, &stream);
    CURLcode code = curl_easy_perform(handle_);
    curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &response.status);
    if (code != CURLE_OK && !stream.stopped) {
        response.error = describe(code, detail, user_ca_, !cert_path_.empty());
    }
    response.body = std::move(stream.error_body);
    curl_slist_free_all(list);
    return response;
}

}  // namespace app
