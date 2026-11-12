// SPDX-License-Identifier: GPL-3.0-or-later
#include "http.hpp"

#include <curl/curl.h>
#include <switch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "dreadovania/sha256.hpp"
#include "dreadovania/version.hpp"

namespace app {

namespace {

constexpr const char* kUserCa = "sdmc:/config/dreadovania/ca.pem";
// The user's CA certificates, one per file: the TLS service imports one
// certificate per call, and curl's libnx backend imports each file in
// CURLOPT_CAPATH separately (ADR 0006).
constexpr const char* kUserCaDir = "sdmc:/config/dreadovania/trusted";
// Mozilla's roots (romfs/cacert.pem) and the user's CA, one certificate per
// file, for servers the Switch's own roots don't cover (ADR 0013). The stamp is
// kept outside the folder: the TLS backend imports every file in it.
constexpr const char* kBundledRoots = "romfs:/cacert.pem";
// One folder per batch of at most kRootsPerBatch roots, plus the user's CA: the
// TLS service refuses to import more than about 70 into a connection (ADR 0014).
constexpr const char* kPublicCaDir = "sdmc:/config/dreadovania/trusted-public";
constexpr const char* kPublicCaStamp = "sdmc:/config/dreadovania/trusted-public.stamp";
constexpr std::size_t kRootsPerBatch = 32;
constexpr std::size_t kMaxBatchFolders = 16;  // folders of an older batch size are cleared too

std::string batch_folder(std::size_t i) {
    return std::string(kPublicCaDir) + "-" + std::to_string(i);
}
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

// curl's progress callback: let the screen move while a request waits.
int on_wait(void* hook, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    (*static_cast<std::function<void()>*>(hook))();
    return 0;  // carry on
}

// One attempt's result, briefly, for the list in the error message (ADR 0015).
std::string short_result(CURLcode code, const char* detail) {
    std::string out;
    switch (code) {
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
            out = "not trusted";
            break;
        case CURLE_SSL_CACERT_BADFILE:
            out = "roots not loaded";
            break;
        case CURLE_SSL_CIPHER:
            out = "cipher refused";
            break;
        case CURLE_SSL_CONNECT_ERROR:
            out = "TLS error";
            break;
        default:
            out = curl_easy_strerror(code);
    }
    std::string why(detail);
    if (!why.empty()) {  // e.g. "verify 0x..., RSA cert" from our curl patch (ADR 0015)
        if (std::size_t libnx = why.find("libnx: "); libnx != std::string::npos) {
            why = why.substr(libnx + 7);  // just the result code
        }
        out += " (" + why.substr(0, 60) + ")";
    }
    return out;
}

// " Tried: Switch: not trusted; own CA, RSA: cipher refused (0x...)" when more
// than one attempt was made, so a failure says what each one did.
std::string attempts_summary(const std::vector<std::string>& tried) {
    if (tried.size() < 2) {
        return "";
    }
    std::string out = ". Tried: ";
    for (std::size_t i = 0; i < tried.size(); ++i) {
        out += (i ? "; " : "") + tried[i];
    }
    return out;
}

// The TLS service didn't trust the server's certificate (see describe()).
bool untrusted(CURLcode code) {
    return code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CERTPROBLEM ||
           code == CURLE_SSL_CACERT_BADFILE;
}

void clear_folder(const char* path) {
    if (DIR* dir = opendir(path)) {
        while (dirent* entry = readdir(dir)) {
            if (entry->d_name[0] != '.') {
                std::remove((std::string(path) + "/" + entry->d_name).c_str());
            }
        }
        closedir(dir);
    }
}

bool write_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

std::string describe(CURLcode code, const char* detail, dreadovania::UserCa ca,
                     bool has_certificate, bool public_roots_tried) {
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
            return dreadovania::untrusted_certificate_message(detail, ca, switch_time_utc(),
                                                              public_roots_tried);
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

std::string Http::init(const std::function<void(const std::string&)>& on_step) {
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
    clear_folder(kUserCaDir);  // forget certificates from an older ca.pem
    std::string user_ca;
    bool has_user_ca = read_file(kUserCa, user_ca);
    user_ca_ = dreadovania::classify_user_ca(user_ca, has_user_ca);
    std::vector<std::string> user_certs;
    if (user_ca_ == dreadovania::UserCa::Loaded) {
        user_certs = dreadovania::split_pem_certificates(user_ca);
        for (std::size_t i = 0; i < user_certs.size(); ++i) {
            std::string path = std::string(kUserCaDir) + "/ca-" + std::to_string(i) + ".pem";
            if (!write_file(path, user_certs[i])) {
                shutdown();
                return "couldn't write to the SD card (/config/dreadovania)";
            }
        }
        user_ca_count_ = user_certs.size();
        ca_dir_ = kUserCaDir;
    }
    prepare_public_roots(user_certs, on_step);
    plan_attempts();
    agent_ = "Dreadovania-NX/" + std::string(dreadovania::kVersion);
    ready_ = true;
    return "";
}

void Http::prepare_public_roots(const std::vector<std::string>& user_certs,
                                const std::function<void(const std::string&)>& on_step) {
    // Mozilla's roots in batches, each with the user's CA, rewritten only when the
    // bundle or ca.pem changes, so the SD card isn't written ~120 files at every
    // start (ADRs 0013 and 0014).
    std::string bundle;
    bool read = R_SUCCEEDED(romfsInit());
    if (read) {
        read = read_file(kBundledRoots, bundle);
        romfsExit();
    }
    std::vector<std::vector<std::string>> batches =
        read ? dreadovania::root_batches(bundle, kRootsPerBatch)
             : std::vector<std::vector<std::string>>{};
    if (batches.empty() || batches.size() > kMaxBatchFolders) {
        return;  // no bundle: the Switch's roots and the user's CA only
    }
    dreadovania::Sha256 hash;
    hash.update(bundle);
    hash.update("\n--batch--\n" + std::to_string(kRootsPerBatch));
    for (const std::string& cert : user_certs) {
        hash.update("\n--user--\n");
        hash.update(cert);
    }
    std::string stamp = hash.hex();
    std::string written;
    if (!read_file(kPublicCaStamp, written) || written != stamp) {
        if (on_step) {
            on_step("Preparing trusted certificates...");
        }
        std::remove(kPublicCaStamp);  // half-written folders: not used until finished
        clear_folder(kPublicCaDir);   // 0.8.0's single folder, which was too big
        rmdir(kPublicCaDir);
        for (std::size_t i = 0; i < kMaxBatchFolders; ++i) {
            clear_folder(batch_folder(i).c_str());
            if (i >= batches.size()) {
                rmdir(batch_folder(i).c_str());
            }
        }
        for (std::size_t i = 0; i < batches.size(); ++i) {
            if (on_step) {
                on_step("Preparing trusted certificates (" + std::to_string(i + 1) + " of " +
                        std::to_string(batches.size()) + ")...");
            }
            std::string folder = batch_folder(i);
            mkdir(folder.c_str(), 0777);
            for (std::size_t k = 0; k < batches[i].size(); ++k) {
                if (!write_file(folder + "/mozilla-" + std::to_string(k) + ".pem", batches[i][k])) {
                    return;
                }
            }
            for (std::size_t k = 0; k < user_certs.size(); ++k) {
                if (!write_file(folder + "/ca-" + std::to_string(k) + ".pem", user_certs[k])) {
                    return;
                }
            }
        }
        if (!write_file(kPublicCaStamp, stamp)) {
            return;
        }
    }
    for (std::size_t i = 0; i < batches.size(); ++i) {
        public_ca_dirs_.push_back(batch_folder(i));
        public_root_count_ += batches[i].size();
    }
}

void Http::plan_attempts() {
    // After the first try (the Switch's roots and the user's CA), each folder in
    // turn, plainly and then asking for RSA: a server whose ECDSA chain the
    // Switch can't check may have an RSA one too, as Cloudflare does (ADR 0015).
    bool rsa = hosversionAtLeast(17, 0, 0);  // libnx's SetCipher option
    if (rsa) {
        attempts_.push_back({&ca_dir_, true});
    }
    for (const std::string& folder : public_ca_dirs_) {
        attempts_.push_back({&folder, false});
        if (rsa) {
            attempts_.push_back({&folder, true});
        }
    }
}

const Http::Attempt* Http::attempt_for(const std::string& url) const {
    auto it = attempt_of_.find(dreadovania::origin_of(url));
    return it != attempt_of_.end() ? &attempts_[it->second] : nullptr;
}

std::string Http::attempt_label(const std::string& url) const {
    const Attempt* attempt = attempt_for(url);
    if (attempt == nullptr) {
        return "Switch";
    }
    std::string where = "own CA";
    for (std::size_t i = 0; i < public_ca_dirs_.size(); ++i) {
        if (attempt->folder == &public_ca_dirs_[i]) {
            where = "roots " + std::to_string(i + 1);
        }
    }
    return attempt->rsa ? where + ", RSA" : where;
}

bool Http::with_public_roots(const std::string& url) const {
    return attempt_of_.count(dreadovania::origin_of(url)) != 0;
}

bool Http::next_attempt(const std::string& url) {
    // The one that works is remembered for the server until the app restarts
    // (ADRs 0014, 0015). Every attempt verifies the certificate fully.
    std::string origin = dreadovania::origin_of(url);
    if (attempts_.empty() || origin.empty()) {
        return false;
    }
    auto it = attempt_of_.find(origin);
    std::size_t next = it == attempt_of_.end() ? 0 : it->second + 1;
    if (next >= attempts_.size()) {
        attempt_of_.erase(origin);  // none helped: start plainly next time
        return false;
    }
    attempt_of_[origin] = next;
    return true;
}

bool Http::try_next(int result, const std::string& url) {
    auto code = static_cast<CURLcode>(result);
    // Untrusted, or an RSA attempt the TLS service turned down: try the next one.
    const Attempt* current = attempt_for(url);
    bool rsa_refused = current != nullptr && current->rsa &&
                       (code == CURLE_SSL_CIPHER || code == CURLE_SSL_CONNECT_ERROR);
    return (untrusted(code) || rsa_refused) && next_attempt(url);
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
    // No CA file: the Switch's trusted roots, plus the user's CA if there is one,
    // plus Mozilla's roots for a server that needed them (ADR 0013).
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);
    const Attempt* attempt = attempt_for(url);
    const std::string& folder = attempt != nullptr ? *attempt->folder : ca_dir_;
    if (!folder.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAPATH, folder.c_str());
    }
    if (attempt != nullptr && attempt->rsa) {
        // One suite, ECDHE-RSA-AES128-GCM-SHA256, by its IANA number: our curl
        // patch passes it to libnx's SetCipher (ADR 0015). TLS 1.2 only, as below.
        curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "0xC02F");
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
    if (wait_hook_) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_wait);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &wait_hook_);
    }
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
    CURLcode code = CURLE_OK;
    bool retried = false;
    std::vector<std::string> tried;  // each failed attempt, for the message
    do {
        // A certificate is checked before anything is sent, so trying again is safe.
        if (code != CURLE_OK) {
            retried = true;
            response.body.clear();
            detail[0] = '\0';
        }
        std::string label = attempt_label(url);
        setup(url, method_str.c_str(), list, detail);
        curl_easy_setopt(handle_, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, on_data);
        curl_easy_setopt(handle_, CURLOPT_WRITEDATA, &sink);
        if (!body.empty()) {
            curl_easy_setopt(handle_, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(handle_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }
        code = curl_easy_perform(handle_);
        if (code != CURLE_OK) {
            tried.push_back(label + ": " + short_result(code, detail));
        }
    } while (try_next(code, url));
    if (code == CURLE_OK) {
        curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &response.status);
    } else if (sink.too_big) {
        response.error = "the server's answer was too large";
    } else {
        response.error = describe(code, detail, user_ca_, !cert_path_.empty(),
                                  retried || with_public_roots(url)) +
                         attempts_summary(tried);
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
    Stream stream{&on_data, &on_progress, 0, 0, nullptr, false, {}};
    CURLcode code = CURLE_OK;
    bool retried = false;
    std::vector<std::string> tried;
    do {
        // The certificate is checked before any data arrives, so on_data hasn't
        // been called when trying again with Mozilla's roots (ADR 0013).
        if (code != CURLE_OK) {
            retried = true;
            detail[0] = '\0';
        }
        std::string label = attempt_label(url);
        setup(url, "GET", list, detail);
        stream = Stream{&on_data, &on_progress, 0, 0, handle_, false, {}};
        // No overall time limit for a download; give up if it stalls for 30 seconds.
        curl_easy_setopt(handle_, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(handle_, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, on_stream);
        curl_easy_setopt(handle_, CURLOPT_WRITEDATA, &stream);
        code = curl_easy_perform(handle_);
        if (code != CURLE_OK) {
            tried.push_back(label + ": " + short_result(code, detail));
        }
    } while (stream.received == 0 && try_next(code, url));
    curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &response.status);
    if (code != CURLE_OK && !stream.stopped) {
        response.error = describe(code, detail, user_ca_, !cert_path_.empty(),
                                  retried || with_public_roots(url)) +
                         attempts_summary(tried);
    }
    response.body = std::move(stream.error_body);
    curl_slist_free_all(list);
    return response;
}

}  // namespace app
