// Copyright 2026 The Aether Authors.
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

// Native Grok companion: serves chat/search UI and proxies to the `grok` CLI.
// Handled on the AgentGateway port (no separate Python process).
// Grok Web handoff: pending prompts for grok.com auto-submit (browser-side).
std::string BuildPageGrokWebPrompt(const std::string& text);
std::string GetGrokWebPendingPrompt(const std::string& id);
void ConsumeGrokWebPendingPrompt(const std::string& id);

base::DictValue LoadCompanionSessions();
void SaveCompanionSessions(const base::DictValue& data);
std::string ResolveConfiguredModel(const std::string* model_override);

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