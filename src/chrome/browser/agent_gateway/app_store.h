// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_APP_STORE_H_
#define CHROME_BROWSER_AGENT_GATEWAY_APP_STORE_H_

#include "base/task/single_thread_task_runner.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"

namespace agent_gateway {

// Grok Build app gallery: registry, folders, per-app conversations, agents.
bool TryHandleAppsRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info,
    net::HttpServer* server,
    int gateway_port,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner);

void OnAppBuildStreamFinished(const std::string& app_id,
                              const std::string& conv_id,
                              int exit_code,
                              const std::string& session_id,
                              const std::string& full_text);

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_APP_STORE_H_