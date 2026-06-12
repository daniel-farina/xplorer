// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/agent_session.h"

#include <utility>

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
  return JSON.stringify({title: document.title, url: location.href, text});
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
  Eval(kExtractTextJs, std::move(cb));
}

void AgentSession::AXTree(ResultCallback cb) {
  SendCommand("Accessibility.getFullAXTree", base::DictValue(),
              std::move(cb));
}

void AgentSession::Screenshot(ResultCallback cb) {
  // AI-native capture: force the renderer to produce compositor frames for the
  // duration of the capture, exactly like tab/video mirroring does. Without
  // this, macOS (and Chromium's own occlusion tracking) suspend compositing
  // for occluded or backgrounded windows and Page.captureScreenshot hangs
  // forever waiting for a frame. Holding a capturer ref means an agent can
  // screenshot ANY tab on demand, visible or not, with no launch flags.
  if (web_contents_) {
    capture_hold_ = web_contents_->IncrementCapturerCount(
        gfx::Size(), /*stay_hidden=*/false, /*stay_awake=*/true,
        /*is_activity=*/true);
  }
  // The HUD now PERSISTS on the page, so hide it just for this capture and
  // restore it after — agent screenshots stay clean, the human still sees it.
  Eval("(()=>{const h=document.getElementById('__aether_hud');"
       "if(h)h.style.visibility='hidden';return 1;})()",
       base::BindOnce(
           [](AgentSession* self, ResultCallback cb, base::DictValue) {
             base::DictValue params;
             params.Set("format", "png");
             params.Set("captureBeyondViewport", false);
             self->SendCommand(
                 "Page.captureScreenshot", std::move(params),
                 base::BindOnce(
                     [](AgentSession* self, ResultCallback cb,
                        base::DictValue r) {
                       self->capture_hold_.RunAndReset();
                       self->Eval(
                           "(()=>{const h=document.getElementById("
                           "'__aether_hud');if(h)h.style.visibility='visible';"
                           "return 1;})()",
                           base::DoNothing());
                       std::move(cb).Run(std::move(r));
                     },
                     self, std::move(cb)));
           },
           this, std::move(cb)));
}

void AgentSession::Click(const std::string& selector, ResultCallback cb) {
  // Resolve the selector in-page and dispatch a trusted click via CDP input
  // events so it is indistinguishable from a user click.
  Eval("(() => { const e = document.querySelector(" +
           base::GetQuotedJSONString(selector) +
           "); if (!e) return null; e.scrollIntoView({block:'center'});"
           "const r = e.getBoundingClientRect();"
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
