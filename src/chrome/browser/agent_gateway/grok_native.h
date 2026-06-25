// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_GROK_NATIVE_H_
#define CHROME_BROWSER_AGENT_GATEWAY_GROK_NATIVE_H_

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"

namespace agent_gateway {

// Resolves the grok CLI binary (GUI apps lack shell PATH).
base::FilePath ResolveGrokBinary();

// Resolves the companion UI directory (bundled Contents/Resources/companion/ui
// first, then dev checkout paths). This is the SINGLE resolver shared with
// grok_companion's native toolbar overlay so both surfaces always read the same
// files. It lives here in agent_gateway because grok_companion depends on
// agent_gateway, not vice-versa (putting it in grok_companion_util would create
// a circular dependency).
base::FilePath CompanionUiDir();

// Native Grok companion: serves chat/search UI and proxies to the `grok` CLI.
// Handled on the AgentGateway port (no separate Python process).
// Grok Web handoff: pending prompts for grok.com auto-submit (browser-side).
std::string BuildPageGrokWebPrompt(const std::string& text);
std::string GetGrokWebPendingPrompt(const std::string& id);
void ConsumeGrokWebPendingPrompt(const std::string& id);

// Appends a structured event to the in-memory gateway log ring (thread-safe;
// callable from any thread). Surfaced via GET /api/logs and the /logs page.
// |level|: "info"|"warn"|"error". |source|: "build"|"runtime"|"api".
// |app_id|/|detail| may be empty; |exit_code| < 0 means "not applicable".
void RecordGatewayLog(const std::string& level,
                      const std::string& source,
                      const std::string& app_id,
                      const std::string& event,
                      const std::string& message,
                      int exit_code,
                      const std::string& detail);

base::DictValue LoadCompanionSessions();
void SaveCompanionSessions(const base::DictValue& data);

// True if a grok run for conversation |conv_id| is currently active (registered
// in the gateway's ActiveRuns map). Used by the scheduler's manual run-now path
// to apply the same 409 "conversation is busy" guard the message handler uses.
bool IsConversationRunActive(const std::string& conv_id);
std::string ResolveConfiguredModel(const std::string* model_override);
std::string ResolveAppBuildModel(const std::string* model_override);

void RunGrokAgentStream(
    net::HttpServer* server,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    int connection_id,
    const std::string& conv_id,
    const std::string& app_id,
    const std::string& message,
    const std::string& session_id,
    const std::string& model,
    const base::FilePath& cwd);

// Headless run dispatch for the background-task scheduler: runs |message| with
// |model| and appends the reply to conversation |target_conv_id|, with NO live
// HTTP connection. Reuses the same machinery as POST /api/conversations/{id}/
// message (append user msg -> register in ActiveRuns -> blocking RunGrokChat ->
// SaveChatAssistantReply), including the busy guard. If |target_conv_id| is
// empty a new conversation is created. The blocking run is posted to a
// base::ThreadPool {MayBlock, USER_VISIBLE} task, so this returns immediately.
//
// |on_done| (optional) is invoked on the ThreadPool task once the run resolves,
// with the final status string ("ok" | "failed" | "skipped") and the conv_id
// the run was appended to (newly minted if |target_conv_id| was empty). The
// scheduler uses it to stamp last_status / last_fire_us back onto the job.
//
// |max_concurrent_tabs|, when > 0, is a SOFT cap on how many browser tabs the
// run should open: we append a one-line instruction to |message| asking the
// agent to open at most that many tabs. This is NOT hard-enforced — the real
// grok agent does not attribute the tabs it opens to a particular task, so the
// gateway cannot count or cap them per run; the cap is only respected to the
// extent the LLM honors the instruction. <= 0 leaves the message untouched.
void DispatchScheduledRun(
    const std::string& message,
    const std::string& model,
    const std::string& target_conv_id,
    int max_concurrent_tabs,
    base::OnceCallback<void(const std::string& status,
                            const std::string& conv_id)> on_done);

// Headless app-build dispatch for the scheduler. Like DispatchScheduledRun, but
// runs a blocking app-build (grok with --cwd <cwd> + the app-build rules — the
// same command POST /apps/{id}/build/stream runs) and appends the reply to
// |target_conv_id| (auto-created if empty). The model defaults via
// ResolveAppBuildModel when |model| is empty. The blocking run is posted to a
// base::ThreadPool {MayBlock, USER_VISIBLE} task, so this returns immediately.
// |on_done| (optional) fires on that task with "ok" | "failed" | "skipped" and
// the conv_id the reply was appended to. Used when a scheduled Job has a cwd.
void DispatchScheduledAppBuild(
    const std::string& message,
    const std::string& model,
    const std::string& cwd,
    const std::string& target_conv_id,
    base::OnceCallback<void(const std::string& status,
                            const std::string& conv_id)> on_done);

class GrokNative {
 public:
  // Returns true if |info| was handled (response sent or async work started).
  static bool TryHandleRequest(
      int connection_id,
      const net::HttpServerRequestInfo& info,
      net::HttpServer* server,
      int gateway_port,
      scoped_refptr<base::SingleThreadTaskRunner> io_task_runner);
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_GROK_NATIVE_H_