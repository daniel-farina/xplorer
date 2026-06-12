// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/agent_gateway.h"

#include <utility>

#include "base/base64.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/logging.h"
#include "base/message_loop/message_pump_type.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/agent_gateway/agent_session.h"
#include "chrome/browser/agent_gateway/tab_ownership.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "components/sessions/core/session_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/server/http_server_request_info.h"
#include "net/server/http_server_response_info.h"
#include "net/http/http_status_code.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace agent_gateway {

namespace {
AgentGateway* g_instance = nullptr;
constexpr int kDefaultPort = 9334;
constexpr int kBacklog = 5;
}  // namespace

// static
AgentGateway* AgentGateway::Start(int port) {
  DCHECK(!g_instance);
  g_instance = new AgentGateway(port ? port : kDefaultPort);
  return g_instance;
}

// static
AgentGateway* AgentGateway::GetInstance() {
  return g_instance;
}

AgentGateway::AgentGateway(int port) : port_(port) {
  token_ = base::Base64Encode(base::RandBytesAsVector(24));

  // Persist the token so local agents can authenticate without flags.
  base::FilePath dir = ProfileManager::GetLastUsedProfile()->GetPath();
  base::WriteFile(dir.AppendASCII("agent_token"), token_);

  server_thread_ = std::make_unique<base::Thread>("AgentGateway");
  server_thread_->StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));
  server_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&AgentGateway::StartServerOnIOThread,
                                base::Unretained(this), port));
}

AgentGateway::~AgentGateway() {
  g_instance = nullptr;
}

void AgentGateway::StartServerOnIOThread(int port) {
  auto socket = std::make_unique<net::TCPServerSocket>(nullptr,
                                                       net::NetLogSource());
  if (socket->ListenWithAddressAndPort("127.0.0.1", port, kBacklog) !=
      net::OK) {
    // Port taken (another Aether instance): fall back to ephemeral.
    socket->ListenWithAddressAndPort("127.0.0.1", 0, kBacklog);
  }
  net::IPEndPoint addr;
  socket->GetLocalAddress(&addr);
  port_ = addr.port();
  server_ = std::make_unique<net::HttpServer>(std::move(socket), this);
  VLOG(1) << "AgentGateway listening on 127.0.0.1:" << port_;

  // Write a FIXED discovery file so any agent finds the gateway without
  // knowing the profile path or branding. This is the canonical way to
  // connect: read ~/.aether/gateway.json -> {port, token}. Solves the
  // "where is the token?" problem that trips agents up.
  base::FilePath home;
  if (base::PathService::Get(base::DIR_HOME, &home)) {
    base::FilePath dir = home.AppendASCII(".aether");
    base::CreateDirectory(dir);
    base::DictValue d;
    d.Set("port", port_);
    d.Set("token", token_);
    d.Set("url", "http://127.0.0.1:" + base::NumberToString(port_));
    d.Set("cdp_url", "ws://127.0.0.1:9333");
    std::string json;
    base::JSONWriter::Write(d, &json);
    base::WriteFile(dir.AppendASCII("gateway.json"), json);
  }
}

bool AgentGateway::CheckAuth(const net::HttpServerRequestInfo& info) {
  auto it = info.headers.find("authorization");
  return it != info.headers.end() && it->second == "Bearer " + token_;
}

void AgentGateway::OnConnect(int connection_id) {}

void AgentGateway::OnHttpRequest(int connection_id,
                                 const net::HttpServerRequestInfo& info) {
  // Unauthenticated discovery: GET /  tells an agent how to authenticate,
  // without leaking the token. Lets a connecting agent orient itself.
  if (info.path == "/" || info.path == "/whoami") {
    net::HttpServerResponseInfo resp(net::HTTP_OK);
    resp.SetBody(
        "{\"service\":\"aether-agent-gateway\",\"auth\":\"Bearer token from "
        "~/.aether/gateway.json\",\"docs\":\"https://github.com/daniel-farina/"
        "aether/blob/master/AGENTS.md\"}",
        "application/json");
    server_->SendResponse(connection_id, resp, TRAFFIC_ANNOTATION_FOR_TESTS);
    return;
  }
  if (!CheckAuth(info)) {
    // 401 (not 404) with a body that says exactly how to authenticate — a 404
    // here made agents think the API was missing and gave up.
    net::HttpServerResponseInfo resp(net::HTTP_UNAUTHORIZED);
    resp.SetBody(
        "{\"error\":\"missing or invalid bearer token\",\"fix\":\"read token "
        "from ~/.aether/gateway.json and send 'Authorization: Bearer "
        "<token>'\"}",
        "application/json");
    server_->SendResponse(connection_id, resp, TRAFFIC_ANNOTATION_FOR_TESTS);
    return;
  }
  // Command execution must happen on the UI thread; hop and reply async.
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&AgentGateway::RouteRequest,
                                weak_factory_.GetWeakPtr(), connection_id,
                                info));
}

void AgentGateway::RouteRequest(int connection_id,
                                const net::HttpServerRequestInfo& info) {
  // Routing table: see header comment. Tab ids are TabStripModel indices
  // qualified by browser session id, e.g. "12:3".
  std::vector<std::string> parts = base::SplitString(
      info.path, "/", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  auto reply = base::BindOnce(
      [](base::WeakPtr<AgentGateway> self, int cid, base::DictValue d) {
        if (!self)
          return;
        std::string json;
        base::JSONWriter::Write(d, &json);
        self->metrics_.bytes_out += static_cast<int64_t>(json.size());
        self->server_thread_->task_runner()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](net::HttpServer* s, int cid, std::string body) {
                  s->Send200(cid, body, "application/json",
                             TRAFFIC_ANNOTATION_FOR_TESTS);
                },
                self->server_.get(), cid, std::move(json)));
      },
      weak_factory_.GetWeakPtr(), connection_id);

  // Count every request flowing through the gateway.
  metrics_.requests++;
  metrics_.bytes_in += static_cast<int64_t>(info.data.size());

  // Agent identity + model: any request may carry "X-Agent-Id" / "X-Agent-Model"
  // to declare who is acting and which model. Header names arrive lowercased.
  std::string agent_id;
  if (auto it = info.headers.find("x-agent-id"); it != info.headers.end())
    agent_id = it->second;
  if (!agent_id.empty())
    metrics_.agent = agent_id;
  if (auto it = info.headers.find("x-agent-model"); it != info.headers.end())
    metrics_.model = it->second;

  // GET /stats — live metrics (also rendered in the in-tab HUD).
  if (parts.size() == 1 && parts[0] == "stats") {
    std::move(reply).Run(metrics_.ToDict());
    return;
  }

  if (parts.empty() || parts[0] != "tabs") {
    base::DictValue err;
    err.Set("error", "unknown route");
    std::move(reply).Run(std::move(err));
    return;
  }

  // GET /tabs — enumerate with full per-tab context for the agent.
  if (parts.size() == 1 && info.method == "GET") {
    base::ListValue tabs;
    for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
      TabStripModel* model = browser->GetTabStripModel();
      content::WebContents* active = model->GetActiveWebContents();
      for (int i = 0; i < model->count(); ++i) {
        content::WebContents* wc = model->GetWebContentsAt(i);
        base::DictValue t;
        t.Set("id",
              base::NumberToString(browser->GetSessionID().id()) + ":" +
                  base::NumberToString(i));
        t.Set("url", wc->GetLastCommittedURL().spec());
        t.Set("title", base::UTF16ToUTF8(wc->GetTitle()));
        // Context: is this the focused tab, and is it still loading?
        t.Set("active", wc == active);
        t.Set("loading", wc->IsLoading());
        t.Set("audible", wc->IsCurrentlyAudible());
        // Ownership: which agent owns this tab (empty == user/unowned).
        TabOwnership* own = TabOwnership::Get(wc);
        t.Set("owner", own ? own->owner : std::string());
        t.Set("label", own ? own->label : std::string());
        t.Set("mine", own && !agent_id.empty() && own->owner == agent_id);
        tabs.Append(std::move(t));
      }
    }
    base::DictValue d;
    d.Set("tabs", std::move(tabs));
    d.Set("agent_id", agent_id);
    std::move(reply).Run(std::move(d));
    return;
  }

  // POST /tabs — open a NEW tab, owned by the requesting agent. This is how an
  // agent isolates a new request: call POST /tabs to get its own tab instead
  // of navigating (and clobbering) one already in use by the user/another agent.
  if (parts.size() == 1 && info.method == "POST") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* url = body ? body->FindString("url") : nullptr;
    const std::string* owner = body ? body->FindString("owner") : nullptr;
    const std::string* label = body ? body->FindString("label") : nullptr;
    NavigateParams params(ProfileManager::GetLastUsedProfile(),
                          GURL(url ? *url : "about:blank"),
                          ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
    base::DictValue d;
    d.Set("ok", true);
    // Stamp ownership on the freshly created tab so future GET /tabs shows it.
    if (params.navigated_or_inserted_contents) {
      TabOwnership* own =
          TabOwnership::GetOrCreate(params.navigated_or_inserted_contents);
      own->owner = owner ? *owner : agent_id;
      if (label)
        own->label = *label;
      d.Set("owner", own->owner);
    }
    std::move(reply).Run(std::move(d));
    return;
  }

  // /tabs/{id}/{verb}
  content::WebContents* wc = nullptr;
  if (parts.size() >= 2) {
    std::vector<std::string> id = base::SplitString(
        parts[1], ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    int sid = 0, index = 0;
    if (id.size() == 2 && base::StringToInt(id[0], &sid) &&
        base::StringToInt(id[1], &index)) {
      for (BrowserWindowInterface* browser :
           GetAllBrowserWindowInterfaces()) {
        if (browser->GetSessionID().id() == sid &&
            index < browser->GetTabStripModel()->count()) {
          wc = browser->GetTabStripModel()->GetWebContentsAt(index);
        }
      }
    }
  }
  if (!wc) {
    base::DictValue err;
    err.Set("error", "tab not found");
    std::move(reply).Run(std::move(err));
    return;
  }

  // POST /tabs/{id}/own — claim/label an existing tab. Lets an agent take
  // ownership of a tab the user (or another agent) created, or relabel its
  // own. Handled here without a CDP session since it only stamps metadata.
  if (parts.size() > 2 && parts[2] == "own") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* owner = body ? body->FindString("owner") : nullptr;
    const std::string* label = body ? body->FindString("label") : nullptr;
    TabOwnership* own = TabOwnership::GetOrCreate(wc);
    own->owner = owner ? *owner : agent_id;
    if (label)
      own->label = *label;
    base::DictValue d;
    d.Set("owner", own->owner);
    d.Set("label", own->label);
    std::move(reply).Run(std::move(d));
    return;
  }

  // Sessions are cached per-connection in ws mode; HTTP mode is one-shot.
  auto session = std::make_unique<AgentSession>(wc);
  AgentSession* s = session.get();
  // Keep the session alive until the callback fires; also tally this tab's
  // outbound bytes for its HUD (guarded by a WeakPtr in case the tab closes).
  const std::string verb_for_hud = parts.size() > 2 ? parts[2] : "";
  auto done = base::BindOnce(
      [](std::unique_ptr<AgentSession>, base::WeakPtr<AgentGateway> self,
         base::WeakPtr<content::WebContents> wcw, std::string verb,
         decltype(reply) reply, base::DictValue d) {
        if (wcw) {
          std::string j;
          base::JSONWriter::Write(d, &j);
          TabOwnership::GetOrCreate(wcw.get())->bytes_out +=
              static_cast<int64_t>(j.size());
          // Refresh the HUD AFTER the action completes (so for navigate it
          // lands on the freshly-loaded page, not the one being torn down).
          // Skip screenshot so the overlay isn't captured.
          if (self && verb != "screenshot")
            self->PokeHud(wcw.get());
        }
        std::move(reply).Run(std::move(d));
      },
      std::move(session), weak_factory_.GetWeakPtr(), wc->GetWeakPtr(),
      verb_for_hud, std::move(reply));

  const std::string verb = parts.size() > 2 ? parts[2] : "";
  auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
  auto str = [&](const char* k) {
    const std::string* v = body ? body->FindString(k) : nullptr;
    return v ? *v : std::string();
  };

  // Per-tab metrics + identity: the HUD on this tab reflects only the agent
  // driving THIS tab, not a global blend across agents.
  TabOwnership* tm = TabOwnership::GetOrCreate(wc);
  tm->requests++;
  tm->bytes_in += static_cast<int64_t>(info.data.size());
  if (!agent_id.empty() && tm->owner.empty())
    tm->owner = agent_id;
  if (auto it = info.headers.find("x-agent-model"); it != info.headers.end())
    tm->model = it->second;
  tm->last_action = verb;

  metrics_.last_action = verb;
  if (verb == "navigate") {
    metrics_.navigations++; tm->navigations++;
    s->Navigate(str("url"), std::move(done));
  } else if (verb == "text") {
    metrics_.reads++; tm->reads++;
    s->ExtractText(std::move(done));
  } else if (verb == "axtree") {
    metrics_.reads++; tm->reads++;
    s->AXTree(std::move(done));
  } else if (verb == "screenshot") {
    metrics_.screenshots++; tm->screenshots++;
    s->Screenshot(std::move(done));
  } else if (verb == "click") {
    metrics_.clicks++; tm->clicks++;
    s->Click(str("selector"), std::move(done));
  } else if (verb == "type") {
    metrics_.types++; tm->types++;
    s->Type(str("selector"), str("text"), std::move(done));
  } else if (verb == "press") {
    metrics_.presses++; tm->presses++;
    s->Press(str("key"), std::move(done));
  } else if (verb == "eval") {
    metrics_.evals++; tm->evals++;
    s->Eval(str("expression"), std::move(done));
  } else {
    base::DictValue err;
    err.Set("error", "unknown verb");
    std::move(done).Run(std::move(err));
    return;
  }
  // HUD refresh now happens in the |done| callback above, after the action
  // completes — so navigations re-show it on the loaded page.
}

void AgentGateway::OnWebSocketRequest(int connection_id,
                                      const net::HttpServerRequestInfo& info) {
  if (!CheckAuth(info)) {
    server_->Close(connection_id);
    return;
  }
  server_->AcceptWebSocket(connection_id, info,
                           TRAFFIC_ANNOTATION_FOR_TESTS);
}

void AgentGateway::OnWebSocketMessage(int connection_id, std::string data) {
  // WS frames carry the same JSON commands as HTTP bodies plus an "id" and
  // "tab"/"verb" fields; replies echo the id. Routing shares RouteRequest
  // via a synthesized request. (Streaming page events: future work.)
  net::HttpServerRequestInfo synthetic;
  auto msg = base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC);
  if (!msg)
    return;
  const std::string* tab = msg->FindString("tab");
  const std::string* verb = msg->FindString("verb");
  synthetic.method = "POST";
  synthetic.path = "/tabs/" + (tab ? *tab : "") + "/" + (verb ? *verb : "");
  base::JSONWriter::Write(*msg, &synthetic.data);
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&AgentGateway::RouteRequest,
                                weak_factory_.GetWeakPtr(), connection_id,
                                synthetic));
}

void AgentGateway::OnClose(int connection_id) {
  ws_sessions_.erase(connection_id);
}

base::DictValue AgentGateway::Metrics::ToDict() const {
  base::DictValue d;
  d.Set("model", model.empty() ? "AI agent" : model);
  d.Set("agent", agent);
  d.Set("requests", static_cast<int>(requests));
  d.Set("kb_in", static_cast<double>(bytes_in) / 1024.0);
  d.Set("kb_out", static_cast<double>(bytes_out) / 1024.0);
  d.Set("navigations", static_cast<int>(navigations));
  d.Set("clicks", static_cast<int>(clicks));
  d.Set("types", static_cast<int>(types));
  d.Set("presses", static_cast<int>(presses));
  d.Set("reads", static_cast<int>(reads));
  d.Set("screenshots", static_cast<int>(screenshots));
  d.Set("evals", static_cast<int>(evals));
  d.Set("last_action", last_action);
  return d;
}

namespace {

// A self-contained, idempotent HUD injected into any tab an agent controls.
// Renders a colorful animated pixel badge ("controlled by <model>") top-right
// and a live metrics strip along the bottom. Marked data-aether-hud so the
// gateway's own text extraction / observe skip it. Re-running just refreshes
// the stats (passed as a JSON object literal) — the animation keeps running.
std::string BuildHudJs(const std::string& stats_json) {
  return R"JS((function(S){
  const ID='__aether_hud';
  let host=document.getElementById(ID);
  if(!host){
    host=document.createElement('div');
    host.id=ID; host.setAttribute('data-aether-hud','1');
    host.style.cssText='all:initial;position:fixed;inset:0;pointer-events:none;z-index:2147483647;';
    (document.documentElement||document.body).appendChild(host);
    const root=host.attachShadow?host.attachShadow({mode:'closed'}):host;
    const wrap=document.createElement('div');
    wrap.innerHTML=`
      <style>
        .badge{position:fixed;top:12px;right:12px;display:flex;align-items:center;gap:8px;
          font:600 12px/1.2 -apple-system,system-ui,sans-serif;color:#e8ecff;
          background:rgba(20,16,46,.82);backdrop-filter:blur(8px);
          border:1px solid rgba(150,130,255,.45);border-radius:10px;padding:6px 10px;
          box-shadow:0 4px 20px rgba(80,40,160,.4);}
        .badge canvas{border-radius:5px;display:block;image-rendering:pixelated;}
        .dot{width:7px;height:7px;border-radius:50%;background:#67e8ff;
          box-shadow:0 0 8px #67e8ff;animation:p 1.1s infinite;}
        @keyframes p{0%,100%{opacity:1}50%{opacity:.25}}
        .bar{position:fixed;left:0;right:0;bottom:0;display:flex;gap:18px;
          align-items:center;font:500 11px/1 ui-monospace,Menlo,monospace;
          color:#cdd6ff;background:linear-gradient(90deg,rgba(18,14,40,.94),rgba(40,20,70,.94));
          border-top:1px solid rgba(150,130,255,.4);padding:7px 14px;}
        .bar b{color:#9b8cff;font-weight:700;} .bar .v{color:#fff;}
        .spark{color:#67e8ff;}
      </style>
      <div class="badge"><canvas width="22" height="22"></canvas>
        <span class="dot"></span><span class="lbl"></span></div>
      <div class="bar"></div>`;
    root.appendChild(wrap);
    host.__root=root;
    // colorful pixel animation
    const cv=root.querySelector('canvas'),ctx=cv.getContext('2d'),N=11,P=2;
    let f=0;
    (function draw(){
      for(let y=0;y<N;y++)for(let x=0;x<N;x++){
        const h=((x*7+y*13+f*4)%360);
        ctx.fillStyle='hsl('+h+',90%,'+(45+30*Math.sin((x+y+f/6)))+'%)';
        ctx.fillRect(x*P,y*P,P,P);
      }
      f++; host.__raf=requestAnimationFrame(draw);
    })();
    host.__last=Date.now();
    // The HUD PERSISTS — it never disappears when the agent stops. Instead it
    // flips a live/idle indicator so the last metrics stay on screen.
    setInterval(()=>{
      const idle=Date.now()-host.__last>4000;
      const sp=root.querySelector('.spark'),dot=root.querySelector('.dot');
      if(sp){sp.textContent=idle?'◾ idle':'◼ live';
        sp.style.color=idle?'#8a93bf':'#67e8ff';}
      if(dot){dot.style.animation=idle?'none':'p 1.1s infinite';
        dot.style.background=idle?'#8a93bf':'#67e8ff';
        dot.style.boxShadow=idle?'none':'0 0 8px #67e8ff';}
    },800);
  }
  const root=host.__root; host.__last=Date.now(); host.style.opacity='1';
  root.querySelector('.lbl').textContent='🤖 '+(S.model||'AI agent')+
    (S.agent?'  ·  '+S.agent:'');
  const kb=n=>n.toFixed(n<10?2:0);
  root.querySelector('.bar').innerHTML=
    '<b>AETHER</b> <span class="spark">▮ live</span>'+
    '<span><b>calls</b> <span class="v">'+S.requests+'</span></span>'+
    '<span><b>↓</b> <span class="v">'+kb(S.kb_in)+'KB</span></span>'+
    '<span><b>↑</b> <span class="v">'+kb(S.kb_out)+'KB</span></span>'+
    '<span><b>nav</b> <span class="v">'+S.navigations+'</span></span>'+
    '<span><b>clicks</b> <span class="v">'+S.clicks+'</span></span>'+
    '<span><b>typed</b> <span class="v">'+S.types+'</span></span>'+
    '<span><b>keys</b> <span class="v">'+S.presses+'</span></span>'+
    '<span><b>reads</b> <span class="v">'+S.reads+'</span></span>'+
    '<span><b>shots</b> <span class="v">'+S.screenshots+'</span></span>'+
    '<span style="margin-left:auto;opacity:.7">last: '+(S.last_action||'-')+'</span>';
})JS" + std::string(")(") + stats_json + ")";
}

}  // namespace

void AgentGateway::PokeHud(content::WebContents* wc) {
  // Build the HUD from THIS tab's own counters/identity, not the global blend.
  TabOwnership* t = TabOwnership::GetOrCreate(wc);
  base::DictValue d;
  d.Set("model", t->model.empty() ? "AI agent" : t->model);
  d.Set("agent", t->owner);
  d.Set("requests", static_cast<int>(t->requests));
  d.Set("kb_in", static_cast<double>(t->bytes_in) / 1024.0);
  d.Set("kb_out", static_cast<double>(t->bytes_out) / 1024.0);
  d.Set("navigations", static_cast<int>(t->navigations));
  d.Set("clicks", static_cast<int>(t->clicks));
  d.Set("types", static_cast<int>(t->types));
  d.Set("presses", static_cast<int>(t->presses));
  d.Set("reads", static_cast<int>(t->reads));
  d.Set("screenshots", static_cast<int>(t->screenshots));
  d.Set("evals", static_cast<int>(t->evals));
  d.Set("last_action", t->last_action);
  std::string stats;
  base::JSONWriter::Write(d, &stats);
  std::string js = BuildHudJs(stats);
  auto session = std::make_unique<AgentSession>(wc);
  AgentSession* s = session.get();
  s->Eval(js, base::BindOnce([](std::unique_ptr<AgentSession>,
                                base::DictValue) {},
                             std::move(session)));
}

}  // namespace agent_gateway
