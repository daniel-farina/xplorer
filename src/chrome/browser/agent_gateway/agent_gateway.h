// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_AGENT_GATEWAY_H_
#define CHROME_BROWSER_AGENT_GATEWAY_AGENT_GATEWAY_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/power_monitor/power_monitor.h"
#include "base/power_monitor/power_observer.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "net/server/http_server.h"

namespace content {
class WebContents;
}

namespace agent_gateway {

class AgentSession;

// AgentGateway is Xplorer's AI-native control plane. It is created once by
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
//
// Sleep/wake recovery: AgentGateway is a base::PowerSuspendObserver. After a
// macOS sleep/wake the bound 127.0.0.1 listening socket is silently
// invalidated and net::HttpServer's accept loop takes the error once and goes
// permanently quiet with no re-listen — the process lives (the Scheduler's
// poll timer keeps the IO thread alive) but `lsof -iTCP:9334` is empty and
// curl returns 000. On OnResume() we hop to server_thread_ and RebindServer():
// destroy the stale HttpServer + socket and create a fresh one (preferring the
// same port to keep the URL stable, ephemeral fallback otherwise), rewriting
// gateway.json so agents re-discover the (possibly new) port.
class AgentGateway : public net::HttpServer::Delegate,
                     public base::PowerSuspendObserver {
 public:
  // Starts the gateway. |port| of 0 picks 9334 or the next free port.
  static AgentGateway* Start(int port);
  static AgentGateway* GetInstance();

  // Deterministic, explicit shutdown. The instance is a leaked raw global whose
  // destructor effectively never runs, so the server thread / HttpServer / the
  // Scheduler poll timer must be torn down here. Called from
  // ChromeBrowserMainParts::PostMainMessageLoopRun(), after the main loop quits
  // but before thread teardown. Idempotent and safe to call when the server
  // thread was never started or has already been stopped. After it returns,
  // GetInstance() yields null.
  void Shutdown();

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

  // base::PowerSuspendObserver:
  // Both callbacks fire on the sequence the observer was registered on — here
  // the gateway's IO server_thread_ (we subscribe from StartServerOnIOThread).
  void OnSuspend() override;
  void OnResume() override;

 private:
  // Live counters for everything flowing through the gateway. Surfaced both at
  // GET /stats and in the in-tab HUD overlay.
  struct Metrics {
    int64_t requests = 0;
    int64_t bytes_in = 0;
    int64_t bytes_out = 0;
    int64_t navigations = 0;
    int64_t clicks = 0;
    int64_t types = 0;
    int64_t presses = 0;
    int64_t reads = 0;        // text + axtree
    int64_t screenshots = 0;
    int64_t evals = 0;
    std::string model;        // declared via X-Agent-Model
    std::string agent;        // declared via X-Agent-Id
    std::string last_action;
    base::DictValue ToDict() const;
  };

  explicit AgentGateway(int port);
  void StartServerOnIOThread(int port);
  // (Re)creates the listening socket + net::HttpServer and rewrites the
  // discovery files. MUST run on server_thread_ (it touches server_ and the
  // socket, both of which are IO-thread affine). Destroys the old server_
  // first so the stale fd is released before the new bind. Called once from
  // StartServerOnIOThread and again after each sleep/wake via OnResume().
  void RebindServer();
  bool CheckAuth(const net::HttpServerRequestInfo& info);
  void RouteRequest(int connection_id,
                    const net::HttpServerRequestInfo& info);
  // Injects/refreshes the "controlled by AI" HUD overlay in |wc|.
  void PokeHud(content::WebContents* wc);

  std::unique_ptr<base::Thread> server_thread_;
  std::unique_ptr<net::HttpServer> server_;
  std::map<int, std::unique_ptr<AgentSession>> ws_sessions_;
  Metrics metrics_;
  int port_ = 0;
  std::string token_;
  base::WeakPtrFactory<AgentGateway> weak_factory_{this};
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_AGENT_GATEWAY_H_
