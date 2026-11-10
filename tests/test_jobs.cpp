// SPDX-License-Identifier: GPL-3.0-or-later
// source/jobs.cpp against tests/fake/http.hpp: generating and patching a seed
// from the Switch, and following the server's jobs.
#include <string>
#include <vector>

#include "check.hpp"
#include "jobs.hpp"

namespace {

const std::string kServer = "https://dread.test";
const std::string kApi = kServer + "/api/v1";

std::string job(const std::string& status, const std::string& extra = "") {
    return R"({"id": "j1", "seed_id": "s1", "kind": "generate", "status": ")" + status + "\"" +
           extra + "}";
}

}  // namespace

TEST(generating_follows_the_job_until_it_succeeds) {
    app::Http http;
    http.routes[kApi + "/seeds"] = {202, R"({"seed_id": "s1", "job_id": "j1"})", ""};
    http.routes[kApi + "/jobs/j1"] = {200, job("queued"), ""};
    std::vector<std::string> shown;
    int waits = 0;
    app::JobResult result = app::generate_seed(
        http, kServer, "dvt_x", "starter_preset", true,
        [&](const std::string& line) { shown.push_back(line); },
        [&] {
            ++waits;
            http.routes[kApi + "/jobs/j1"] = {
                200, waits < 2 ? job("running", R"(, "progress": "Placing items")") : job("succeeded"),
                ""};
            return true;
        });
    CHECK(result.ok && !result.stopped);
    CHECK(result.seed_id == "s1");
    CHECK(waits == 2);
    CHECK(http.calls.front() ==
          "POST " + kApi + R"(/seeds {"preset_id": "starter_preset", "race": true})");
    CHECK(shown.size() == 3);
    CHECK(shown[1] == "Generating: waiting for the server...");
    CHECK(shown[2] == "Generating: Placing items");
}

TEST(a_failed_job_says_why) {
    app::Http http;
    http.routes[kApi + "/seeds/s1/mod"] = {202, R"({"seed_id": "s1", "job_id": "j1"})", ""};
    http.routes[kApi + "/jobs/j1"] = {200, job("failed", R"(, "error": "out of disk space")"), ""};
    app::JobResult result =
        app::patch_seed(http, kServer, "dvt_x", "s1", [](const std::string&) {}, [] { return true; });
    CHECK(!result.ok);
    CHECK(result.seed_id == "s1");
    CHECK(result.message == "Patching failed on the server: out of disk space");
    http.routes[kApi + "/jobs/j1"] = {200, job("cancelled"), ""};
    result = app::follow_job(http, kServer, "dvt_x", "j1", "Patching", [](const std::string&) {},
                             [] { return true; });
    CHECK(result.message == "Patching was cancelled on the server.");
}

TEST(the_servers_refusal_is_shown) {
    app::Http http;
    http.routes[kApi + "/seeds/s1/mod"] = {
        409, R"({"title": "Can't patch this seed", "detail": "the game files aren't ready: none uploaded"})",
        ""};
    app::JobResult result =
        app::patch_seed(http, kServer, "dvt_x", "s1", [](const std::string&) {}, [] { return true; });
    CHECK(!result.ok);
    CHECK(result.message == "Not patched: the game files aren't ready: none uploaded (HTTP 409)");
    CHECK(http.calls.size() == 1);  // no job to follow
}

TEST(stopping_the_wait_leaves_the_job_running) {
    app::Http http;
    http.routes[kApi + "/jobs/j1"] = {200, job("running"), ""};
    app::JobResult result = app::follow_job(http, kServer, "dvt_x", "j1", "Generating",
                                            [](const std::string&) {}, [] { return false; });
    CHECK(!result.ok && result.stopped);
    CHECK(result.message.find("The server carries on") != std::string::npos);
    for (const std::string& call : http.calls) {
        CHECK(call.rfind("GET ", 0) == 0);  // nothing was cancelled
    }
}

TEST(a_dropped_answer_is_retried_but_a_missing_job_is_not) {
    app::Http http;
    int waits = 0;
    http.routes[kApi + "/jobs/j1"] = {0, "", "the server took too long to answer"};
    app::JobResult result = app::follow_job(
        http, kServer, "dvt_x", "j1", "Generating", [](const std::string&) {}, [&] {
            if (++waits == 2) {
                http.routes[kApi + "/jobs/j1"] = {200, job("succeeded"), ""};
            }
            return true;
        });
    CHECK(result.ok);
    http.routes[kApi + "/jobs/j1"] = {0, "", "the server took too long to answer"};
    result = app::follow_job(http, kServer, "dvt_x", "j1", "Generating",
                             [](const std::string&) {}, [] { return true; });
    CHECK(!result.ok && !result.stopped);
    CHECK(result.message == "Generating couldn't be followed: the server took too long to answer");
    result = app::follow_job(http, kServer, "dvt_x", "gone", "Generating",
                             [](const std::string&) {}, [] { return true; });
    CHECK(result.message == "Generating couldn't be followed: not found (HTTP 404)");
}
