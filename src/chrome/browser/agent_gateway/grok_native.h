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