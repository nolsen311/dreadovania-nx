// SPDX-License-Identifier: GPL-3.0-or-later
#include "jobs.hpp"

#include "dreadovania/api.hpp"

namespace app {

namespace {

constexpr int kMaxPolls = 60 * 60;  // about an hour of one-second waits

std::string explain(const Response& r, const char* fallback) {
    if (!r.error.empty()) {
        return r.error;
    }
    auto detail = dreadovania::problem_detail(r.body);
    return (detail ? *detail : std::string(fallback)) + " (HTTP " + std::to_string(r.status) + ")";
}

}  // namespace

JobResult follow_job(Http& http, const std::string& server, const std::string& token,
                     const std::string& job_id, const std::string& what, const Status& status,
                     const Wait& wait) {
    JobResult result;
    const std::string url = server + "/api/v1/jobs/" + job_id;
    int failures = 0;
    for (int poll = 0; poll < kMaxPolls; ++poll) {
        Response r = http.request("GET", url, "", token);
        auto job = r.status == 200 ? dreadovania::parse_job(r.body) : std::nullopt;
        if (!job) {
            // A dropped answer now and then is fine: the job carries on regardless.
            if (r.status == 404 || ++failures >= 5) {
                result.message = what + " couldn't be followed: " +
                                 explain(r, "unexpected answer");
                return result;
            }
        } else {
            failures = 0;
            if (job->status == "succeeded") {
                result.ok = true;
                return result;
            }
            if (job->status == "failed") {
                result.message = what + " failed on the server" +
                                 (job->error.empty() ? "." : ": " + job->error);
                return result;
            }
            if (job->status == "cancelled") {
                result.message = what + " was cancelled on the server.";
                return result;
            }
            status(what + (job->status == "queued" ? ": waiting for the server..."
                           : job->progress.empty() ? "..."
                                                   : ": " + job->progress));
        }
        if (!wait()) {
            result.stopped = true;
            result.message = "Stopped waiting. The server carries on: the seed will be in the "
                             "list, and A picks up where this left off.";
            return result;
        }
    }
    result.message = what + " is taking too long. It's still on the server: press A on it later.";
    return result;
}

JobResult generate_seed(Http& http, const std::string& server, const std::string& token,
                        const std::string& preset_id, bool race, const Status& status,
                        const Wait& wait) {
    status("Asking the server for a new seed...");
    Response r = http.request("POST", server + "/api/v1/seeds",
                              dreadovania::generate_request_body(preset_id, race), token);
    auto submitted = r.status == 202 ? dreadovania::parse_submitted(r.body) : std::nullopt;
    if (!submitted) {
        JobResult result;
        result.message = "Not generated: " + explain(r, "the server refused");
        return result;
    }
    JobResult result = follow_job(http, server, token, submitted->job_id, "Generating", status,
                                  wait);
    result.seed_id = submitted->seed_id;
    return result;
}

JobResult patch_seed(Http& http, const std::string& server, const std::string& token,
                     const std::string& seed_id, const Status& status, const Wait& wait) {
    status("Asking the server to patch the seed...");
    Response r = http.request("POST", server + "/api/v1/seeds/" + seed_id + "/mod", "", token);
    auto submitted = r.status == 202 ? dreadovania::parse_submitted(r.body) : std::nullopt;
    if (!submitted) {
        JobResult result;
        result.seed_id = seed_id;
        result.message = "Not patched: " + explain(r, "the server refused");
        return result;
    }
    JobResult result = follow_job(http, server, token, submitted->job_id, "Patching", status,
                                  wait);
    result.seed_id = seed_id;
    return result;
}

}  // namespace app
