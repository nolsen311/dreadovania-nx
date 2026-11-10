// SPDX-License-Identifier: GPL-3.0-or-later
// Generating and patching a seed on the server from the Switch, and following
// the server's job until it finishes (server routes POST /seeds,
// POST /seeds/{id}/mod and GET /jobs/{id}, all open to a paired device).
#pragma once

#include <functional>
#include <string>

// Through the include path, so the host tests can supply their own (tests/fake).
#include <http.hpp>

namespace app {

struct JobResult {
    bool ok = false;        // the job succeeded
    bool stopped = false;   // the user stopped waiting; the server carries on
    std::string seed_id;
    std::string message;    // for the user when not ok
};

// Shows a line of progress.
using Status = std::function<void(const std::string&)>;
// Waits about a second between checks. Returns false to stop waiting.
using Wait = std::function<bool()>;

// Follows a job until it finishes. `what` names it for messages ("Generating").
JobResult follow_job(Http& http, const std::string& server, const std::string& token,
                     const std::string& job_id, const std::string& what, const Status& status,
                     const Wait& wait);

// Starts a new seed from a preset, then follows its job.
JobResult generate_seed(Http& http, const std::string& server, const std::string& token,
                        const std::string& preset_id, bool race, const Status& status,
                        const Wait& wait);

// Starts patching a generated seed with the server's game files, then follows it.
JobResult patch_seed(Http& http, const std::string& server, const std::string& token,
                     const std::string& seed_id, const Status& status, const Wait& wait);

}  // namespace app
