// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_AGENT_GATEWAY_H_
#define CHROME_BROWSER_AGENT_GATEWAY_AGENT_GATEWAY_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/threading/thread.h"
#include "net/server/http_server.h"

namespace agent_gateway {

class AgentSession;

// AgentGateway is Aether's AI-native control plane. It is created once by
// ChromeBrowserMainParts after profile init and lives for the lifetime of
// the browser process. It runs a localhost HTTP/WebSocket server (default
// port 9334) offering high-level, single-round-trip primitives for agents:
//
//   GET  /tabs                       list open tabs
//   POST /tabs                       {"url": ...} -> open tab, returns id
//   POST /tabs/{id}/navigate         {"url": ...}, waits for load
//   GET  /tabs/{id}/text             readability-style text extraction
//   GET  /tabs/{id}/axtree           accessibility tree snapshot (JSON)
//   GET  /tabs/{id}/screenshot       PNG
//   POST /tabs/{id}/click            {"selector": ...} or {"x":..,"y":..}
//   POST /tabs/{id}/type             {"selector": ..., "text": ...}
//   POST /tabs/{id}/eval             {"expression": ...} -> JSON result
//   DELETE /tabs/{id}                close tab
//   WS   /session                    bidi stream of the above + page events
//
// Internally each command is executed against the tab's in-process
// content::DevToolsAgentHost, so there is no second protocol hop.
// Authentication: every request must carry the bearer token written to
// <profile>/agent_token at startup. The server binds 127.0.0.1 only.
class AgentGateway : public net::HttpServer::Delegate {
 public:
  // Starts the gateway. |port| of 0 picks 9334 or the next free port.
  static AgentGateway* Start(int port);
  static AgentGateway* GetInstance();

  AgentGateway(const AgentGateway&) = delete;
  AgentGateway& operator=(const AgentGateway&) = delete;
  ~AgentGateway() override;

  int port() const { return port_; }
  const std::string& token() const { return token_; }

  // net::HttpServer::Delegate:
  void OnConnect(int connection_id) override;
  void OnHttpRequest(int connection_id,
                     const net::HttpServerRequestInfo& info) override;
  void OnWebSocketRequest(int connection_id,
                          const net::HttpServerRequestInfo& info) override;
  void OnWebSocketMessage(int connection_id, std::string data) override;
  void OnClose(int connection_id) override;

 private:
  explicit AgentGateway(int port);
  void StartServerOnIOThread(int port);
  bool CheckAuth(const net::HttpServerRequestInfo& info);
  void RouteRequest(int connection_id,
                    const net::HttpServerRequestInfo& info);

  std::unique_ptr<base::Thread> server_thread_;
  std::unique_ptr<net::HttpServer> server_;
  std::map<int, std::unique_ptr<AgentSession>> ws_sessions_;
  int port_ = 0;
  std::string token_;
  base::WeakPtrFactory<AgentGateway> weak_factory_{this};
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_AGENT_GATEWAY_H_
