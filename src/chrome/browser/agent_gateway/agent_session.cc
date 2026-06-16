// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/agent_session.h"

#include <utility>

#include <memory>

#include "chrome/browser/agent_gateway/tab_screenshot.h"

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/size.h"

namespace agent_gateway {

namespace {

// Readability-style extraction run in the page. Strips chrome, nav, ads;
// returns title + main text. Kept dependency-free so it works on any page.
constexpr char kExtractTextJs[] = R"js(
(() => {
  const kill = 'script,style,noscript,svg,nav,footer,header,aside,iframe';
  const doc = document.cloneNode(true);
  doc.querySelectorAll(kill).forEach(n => n.remove());
  const main = doc.querySelector('main,article,[role=main]') || doc.body;
  const text = (main ? main.innerText : '').replace(/\n{3,}/g, '\n\n').trim();
  // Show the agent the region it's reading.
  if (main && window.__xplorerHL) { const r = main.getBoundingClientRect();
    window.__xplorerHL(r.x, r.y, r.width, Math.min(r.height, innerHeight), 'read'); }
  return JSON.stringify({title: document.title, url: location.href, text});
})()
)js";

// Defines window.__xplorerHL once: a live "agent is looking here" visualizer.
// Draws a transient, color-coded box over a viewport rect — click=pink,
// type=blue, read=green, scan=cyan, link=gold. Respects a per-site on/off
// toggle in localStorage (on by default). Marked data-xplorer-hud so the
// gateway's own text/observe reads skip it. Idempotent — safe to prepend to
// any action's eval.
constexpr char kHLEnsure[] = R"js(
(() => {
  if (window.__xplorerHL) return;
  const C = document.createElement('div');
  C.setAttribute('data-xplorer-hud', '1');
  C.style.cssText = 'all:initial;position:fixed;inset:0;pointer-events:none;'+
    'z-index:2147483646';
  (document.documentElement || document.body).appendChild(C);
  const COL = {click:'#ff3da6', type:'#3da6ff', read:'#36e07f',
               scan:'#67e8ff', link:'#ffd23d'};
  window.__xplorerHL = (x, y, w, h, kind) => {
    try { if (localStorage.getItem('__xplorer_hl') === 'off') return; } catch(e){}
    if (w <= 0 || h <= 0) return;
    const c = COL[kind] || '#fff';
    const b = document.createElement('div');
    b.style.cssText = 'position:fixed;left:'+x+'px;top:'+y+'px;width:'+w+'px;'+
      'height:'+h+'px;border:2px solid '+c+';border-radius:4px;box-sizing:border-box;'+
      'box-shadow:0 0 12px '+c+';background:'+c+'1f;pointer-events:none;'+
      'transition:opacity .7s ease;opacity:.95;';
    C.appendChild(b);
    setTimeout(() => { b.style.opacity = '0'; }, kind === 'scan' ? 450 : 600);
    setTimeout(() => { b.remove(); }, 1300);
  };
})()
)js";

}  // namespace

AgentSession::AgentSession(content::WebContents* web_contents)
    : host_(content::DevToolsAgentHost::GetOrCreateFor(web_contents)),
      web_contents_(web_contents->GetWeakPtr()) {
  host_->AttachClient(this);
  SendCommand("Page.enable", base::DictValue(), base::DoNothing());
}

AgentSession::~AgentSession() {
  if (host_)
    host_->DetachClient(this);
}

void AgentSession::SendCommand(const std::string& method,
                               base::DictValue params,
                               ResultCallback cb) {
  int id = next_id_++;
  pending_[id] = std::move(cb);
  base::DictValue msg;
  msg.Set("id", id);
  msg.Set("method", method);
  msg.Set("params", std::move(params));
  std::string json;
  base::JSONWriter::Write(msg, &json);
  host_->DispatchProtocolMessage(this, base::as_byte_span(json));
}

void AgentSession::DispatchProtocolMessage(content::DevToolsAgentHost* host,
                                           base::span<const uint8_t> message) {
  auto parsed = base::JSONReader::ReadDict(
      std::string_view(reinterpret_cast<const char*>(message.data()),
                       message.size()),
      base::JSON_PARSE_RFC);
  if (!parsed)
    return;

  // Command responses.
  if (std::optional<int> id = parsed->FindInt("id")) {
    auto it = pending_.find(*id);
    if (it != pending_.end()) {
      ResultCallback cb = std::move(it->second);
      pending_.erase(it);
      base::DictValue* result = parsed->FindDict("result");
      std::move(cb).Run(result ? std::move(*result) : base::DictValue());
    }
    return;
  }

  // Events: resolve navigation waiters on load.
  const std::string* method = parsed->FindString("method");
  if (method && *method == "Page.loadEventFired" && load_waiter_) {
    base::DictValue ok;
    ok.Set("loaded", true);
    std::move(load_waiter_).Run(std::move(ok));
  }
}

void AgentSession::AgentHostClosed(content::DevToolsAgentHost* host) {
  host_ = nullptr;
}

void AgentSession::Navigate(const std::string& url, ResultCallback cb) {
  // Gate the navigation on the Page.enable ack: the ctor enables Page events
  // asynchronously, and issuing Page.navigate before that ack lands means the
  // resulting Page.loadEventFired is never delivered and the caller hangs.
  load_waiter_ = std::move(cb);
  SendCommand(
      "Page.enable", base::DictValue(),
      base::BindOnce(
          [](AgentSession* self, std::string url, base::DictValue) {
            base::DictValue params;
            params.Set("url", url);
            self->SendCommand("Page.navigate", std::move(params),
                              base::DoNothing());
          },
          this, url));
}

void AgentSession::Eval(const std::string& expression, ResultCallback cb) {
  base::DictValue params;
  params.Set("expression", expression);
  params.Set("returnByValue", true);
  params.Set("awaitPromise", true);
  SendCommand("Runtime.evaluate", std::move(params), std::move(cb));
}

void AgentSession::ExtractText(ResultCallback cb) {
  Eval(std::string(kHLEnsure) + ";" + kExtractTextJs, std::move(cb));
}

void AgentSession::AXTree(ResultCallback cb) {
  SendCommand("Accessibility.getFullAXTree", base::DictValue(),
              std::move(cb));
}

void AgentSession::Screenshot(ResultCallback cb) {
  CaptureTabScreenshot(web_contents_.get(), std::move(cb));
}

void AgentSession::Click(const std::string& selector, ResultCallback cb) {
  // Resolve the selector in-page and dispatch a trusted click via CDP input
  // events so it is indistinguishable from a user click.
  Eval(std::string(kHLEnsure) + ";(() => { const e = document.querySelector(" +
           base::GetQuotedJSONString(selector) +
           "); if (!e) return null; e.scrollIntoView({block:'center'});"
           "const r = e.getBoundingClientRect();"
           "if (window.__xplorerHL) window.__xplorerHL(r.x,r.y,r.width,r.height,"
           "'click');"
           "return JSON.stringify({x: r.x + r.width/2, y: r.y + r.height/2});"
           "})()",
       base::BindOnce(
           [](AgentSession* self, ResultCallback cb, base::DictValue r) {
             const std::string* val =
                 r.FindStringByDottedPath("result.value");
             auto pt = val ? base::JSONReader::ReadDict(*val,
                                                        base::JSON_PARSE_RFC)
                           : std::nullopt;
             if (!pt) {
               base::DictValue err;
               err.Set("error", "selector not found");
               std::move(cb).Run(std::move(err));
               return;
             }
             double x = pt->FindDouble("x").value_or(0);
             double y = pt->FindDouble("y").value_or(0);
             for (const char* type : {"mousePressed", "mouseReleased"}) {
               base::DictValue p;
               p.Set("type", type);
               p.Set("x", x);
               p.Set("y", y);
               p.Set("button", "left");
               p.Set("clickCount", 1);
               self->SendCommand("Input.dispatchMouseEvent", std::move(p),
                                 base::DoNothing());
             }
             base::DictValue ok;
             ok.Set("clicked", true);
             std::move(cb).Run(std::move(ok));
           },
           this, std::move(cb)));
}

namespace {
// Maps a key name to (windowsVirtualKeyCode, DOM key, DOM code). Covers the
// keys agents need to drive forms and menus.
struct KeyInfo {
  int vk;
  const char* key;
  const char* code;
};
std::optional<KeyInfo> LookupKey(const std::string& name) {
  static const base::NoDestructor<std::map<std::string, KeyInfo>> kKeys({
      {"Enter", {13, "Enter", "Enter"}},
      {"Tab", {9, "Tab", "Tab"}},
      {"Escape", {27, "Escape", "Escape"}},
      {"Backspace", {8, "Backspace", "Backspace"}},
      {"ArrowDown", {40, "ArrowDown", "ArrowDown"}},
      {"ArrowUp", {38, "ArrowUp", "ArrowUp"}},
      {"ArrowLeft", {37, "ArrowLeft", "ArrowLeft"}},
      {"ArrowRight", {39, "ArrowRight", "ArrowRight"}},
      {"Space", {32, " ", "Space"}},
  });
  auto it = kKeys->find(name);
  if (it == kKeys->end())
    return std::nullopt;
  return it->second;
}
}  // namespace

void AgentSession::Press(const std::string& key, ResultCallback cb) {
  std::optional<KeyInfo> info = LookupKey(key);
  if (!info) {
    base::DictValue err;
    err.Set("error", "unknown key: " + key);
    std::move(cb).Run(std::move(err));
    return;
  }
  // keyDown (rawKeyDown so it isn't treated as character input) then keyUp.
  for (const char* type : {"rawKeyDown", "keyUp"}) {
    base::DictValue p;
    p.Set("type", type);
    p.Set("windowsVirtualKeyCode", info->vk);
    p.Set("nativeVirtualKeyCode", info->vk);
    p.Set("key", info->key);
    p.Set("code", info->code);
    SendCommand("Input.dispatchKeyEvent", std::move(p), base::DoNothing());
  }
  base::DictValue ok;
  ok.Set("pressed", key);
  std::move(cb).Run(std::move(ok));
}

void AgentSession::Type(const std::string& selector,
                        const std::string& text,
                        ResultCallback cb) {
  Click(selector,
        base::BindOnce(
            [](AgentSession* self, std::string text, ResultCallback cb,
               base::DictValue r) {
              if (r.FindString("error")) {
                std::move(cb).Run(std::move(r));
                return;
              }
              base::DictValue p;
              p.Set("text", text);
              self->SendCommand("Input.insertText", std::move(p),
                                std::move(cb));
            },
            this, text, std::move(cb)));
}

}  // namespace agent_gateway
