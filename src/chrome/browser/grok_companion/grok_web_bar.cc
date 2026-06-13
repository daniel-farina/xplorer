// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/grok_companion/grok_web_bar.h"

#include <map>
#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/values.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/agent_gateway/agent_gateway.h"
#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "url/gurl.h"

namespace grok_companion {
namespace {

constexpr int kMaxInjectAttempts = 12;

bool IsGrokWebHost(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS())
    return false;
  const std::string_view host = url.host();
  return host == "grok.com" || host == "www.grok.com" ||
         base::EndsWith(host, ".grok.com");
}

int GatewayPort() {
  if (auto* gw = agent_gateway::AgentGateway::GetInstance())
    return gw->port();
  return kCompanionPort;
}

GURL SwitchHomeURL(const std::string& mode) {
  return GURL(base::StringPrintf("http://%s:%d/switch-home?mode=%s",
                                 kCompanionHost, GatewayPort(), mode.c_str()));
}

GURL SearchPageURL(const char* search_mode) {
  std::string path = "/search";
  if (search_mode && *search_mode)
    path += std::string("?mode=") + search_mode;
  return GURL(base::StringPrintf("http://%s:%d%s", kCompanionHost,
                                 GatewayPort(), path.c_str()));
}

std::string BuildInjectScript(const std::string& active_mode) {
  const std::string build_href = SwitchHomeURL(kSearchHomeBuild).spec();
  const std::string web_href = SwitchHomeURL(kSearchHomeWeb).spec();
  const std::string search_href = SearchPageURL("").spec();
  const std::string images_href = SearchPageURL("images").spec();
  const std::string videos_href = SearchPageURL("videos").spec();
  const std::string build_active =
      active_mode == kSearchHomeBuild ? " active" : "";
  const std::string web_active = active_mode == kSearchHomeWeb ? " active" : "";

  return base::StringPrintf(
      R"((function(){
  if (document.getElementById('xbrowser-grok-bar')) return;
  var style = document.createElement('style');
  style.textContent = [
    '#xbrowser-grok-bar{',
    'position:fixed;top:0;left:0;right:0;z-index:2147483647;',
    'display:flex;align-items:center;gap:12px;padding:10px 16px;',
    'font:13px/1.4 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;',
    'border-bottom:1px solid var(--xb-border);background:var(--xb-surface);',
    'color:var(--xb-text);box-shadow:0 1px 0 var(--xb-shadow);}',
    '#xbrowser-grok-bar .xb-logo{font-weight:700;font-size:16px;color:var(--xb-accent);',
    'text-decoration:none;white-space:nowrap;}',
    '#xbrowser-grok-bar .xb-modes{display:inline-flex;gap:2px;flex-wrap:wrap;}',
    '#xbrowser-grok-bar .xb-mode{padding:5px 12px;text-decoration:none;color:var(--xb-muted);',
    'font-size:12px;border-radius:999px;white-space:nowrap;}',
    '#xbrowser-grok-bar .xb-mode:hover{color:var(--xb-text);background:var(--xb-elevated);}',
    '#xbrowser-grok-bar .xb-spacer{flex:1;min-width:8px;}',
    '#xbrowser-grok-bar .xb-toggle{display:inline-flex;border:1px solid var(--xb-border);',
    'border-radius:999px;overflow:hidden;background:var(--xb-elevated);}',
    '#xbrowser-grok-bar .xb-opt{padding:5px 12px;text-decoration:none;color:var(--xb-muted);',
    'font-size:12px;white-space:nowrap;}',
    '#xbrowser-grok-bar .xb-opt:hover{color:var(--xb-text);background:var(--xb-surface);}',
    '#xbrowser-grok-bar .xb-opt.active{background:var(--xb-accent-soft);',
    'color:var(--xb-accent);font-weight:500;}',
    '@media (prefers-color-scheme:light){',
    '#xbrowser-grok-bar{--xb-border:#dadce0;--xb-surface:#f8f9fa;--xb-text:#202124;',
    '--xb-muted:#5f6368;--xb-elevated:#f1f3f4;--xb-accent:#1a73e8;',
    '--xb-accent-soft:rgba(26,115,232,.12);--xb-shadow:rgba(60,64,67,.12);}}',
    '@media (prefers-color-scheme:dark){',
    '#xbrowser-grok-bar{--xb-border:#333;--xb-surface:#161616;--xb-text:#f2f2f2;',
    '--xb-muted:#aaa;--xb-elevated:#222;--xb-accent:#f2f2f2;',
    '--xb-accent-soft:#2a2a2a;--xb-shadow:rgba(0,0,0,.25);}}'
  ].join('');
  document.documentElement.appendChild(style);
  var bar = document.createElement('div');
  bar.id = 'xbrowser-grok-bar';
  bar.innerHTML = [
    '<a class="xb-logo" href="%s">✦ Grok</a>',
    '<nav class="xb-modes">',
    '<a class="xb-mode" href="%s">All</a>',
    '<a class="xb-mode" href="%s">Images</a>',
    '<a class="xb-mode" href="%s">Videos</a>',
    '</nav>',
    '<span class="xb-spacer"></span>',
    '<div class="xb-toggle">',
    '<a class="xb-opt%s" href="%s">Grok Build</a>',
    '<a class="xb-opt%s" href="%s">Grok Web</a>',
    '</div>'
  ].join('');
  var root = document.body || document.documentElement;
  root.insertBefore(bar, root.firstChild);
  var pad = (bar.getBoundingClientRect().height || 44) + 'px';
  document.documentElement.style.setProperty('padding-top', pad, 'important');
  if (document.body) document.body.style.setProperty('padding-top', pad, 'important');
})();)",
      search_href.c_str(), search_href.c_str(), images_href.c_str(),
      videos_href.c_str(), build_active.c_str(), build_href.c_str(),
      web_active.c_str(), web_href.c_str());
}

class GrokWebBarInjector : public content::WebContentsObserver {
 public:
  explicit GrokWebBarInjector(content::WebContents* contents)
      : content::WebContentsObserver(contents) {
    ScheduleInject();
  }

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!navigation_handle->IsInPrimaryMainFrame() ||
        !navigation_handle->HasCommitted() ||
        navigation_handle->IsErrorPage()) {
      return;
    }
    if (!IsGrokWebHost(navigation_handle->GetURL()))
      return;
    ScheduleInject();
  }

  void DocumentOnLoadCompletedInPrimaryMainFrame() override {
    ScheduleInject();
  }

 private:
  void ScheduleInject() {
    content::WebContents* contents = web_contents();
    if (!contents || !IsGrokWebHost(contents->GetLastCommittedURL()))
      return;
    inject_attempts_ = 0;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&GrokWebBarInjector::MaybeInject,
                                  weak_factory_.GetWeakPtr()));
  }

  void MaybeInject() {
    content::WebContents* contents = web_contents();
    if (!contents)
      return;
    if (!IsGrokWebHost(contents->GetLastCommittedURL()))
      return;

    content::RenderFrameHost* frame = contents->GetPrimaryMainFrame();
    if (!frame || !frame->IsRenderFrameLive()) {
      RetryInject();
      return;
    }

    frame->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(BuildInjectScript(GetSearchHomeMode())),
        base::BindOnce(&GrokWebBarInjector::OnInjected,
                       weak_factory_.GetWeakPtr()),
        ISOLATED_WORLD_ID_EXTENSIONS);
  }

  void OnInjected(base::Value) { inject_attempts_ = 0; }

  void RetryInject() {
    if (++inject_attempts_ > kMaxInjectAttempts)
      return;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&GrokWebBarInjector::MaybeInject,
                       weak_factory_.GetWeakPtr()),
        base::Milliseconds(250 * inject_attempts_));
  }

  int inject_attempts_ = 0;
  base::WeakPtrFactory<GrokWebBarInjector> weak_factory_{this};
};

class GrokWebBarCoordinator : public TabStripModelObserver {
 public:
  DECLARE_USER_DATA(GrokWebBarCoordinator);

  explicit GrokWebBarCoordinator(BrowserWindowInterface* browser)
      : browser_(browser),
        scoped_unowned_user_data_(browser->GetUnownedUserDataHost(), *this) {
    TabStripModel* model = browser_->GetTabStripModel();
    model->AddObserver(this);
    for (int i = 0; i < model->count(); ++i)
      AttachToWebContents(model->GetWebContentsAt(i));
  }

  GrokWebBarCoordinator(const GrokWebBarCoordinator&) = delete;
  GrokWebBarCoordinator& operator=(const GrokWebBarCoordinator&) = delete;

  ~GrokWebBarCoordinator() override {
    if (browser_)
      browser_->GetTabStripModel()->RemoveObserver(this);
  }

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    if (change.type() == TabStripModelChange::kInserted) {
      for (const auto& item : change.GetInsert()->contents)
        AttachToWebContents(item.contents);
    } else if (change.type() == TabStripModelChange::kReplaced) {
      auto* replace = change.GetReplace();
      injectors_.erase(replace->old_contents);
      AttachToWebContents(replace->new_contents);
    } else if (change.type() == TabStripModelChange::kRemoved) {
      for (const auto& item : change.GetRemove()->contents)
        injectors_.erase(item.contents);
    }
  }

 private:
  void AttachToWebContents(content::WebContents* contents) {
    if (!contents || injectors_.count(contents))
      return;
    injectors_[contents] = std::make_unique<GrokWebBarInjector>(contents);
  }

  raw_ptr<BrowserWindowInterface> browser_;
  ui::ScopedUnownedUserData<GrokWebBarCoordinator> scoped_unowned_user_data_;
  std::map<content::WebContents*, std::unique_ptr<GrokWebBarInjector>>
      injectors_;
};

DEFINE_USER_DATA(GrokWebBarCoordinator);

}  // namespace

void RegisterGrokWebBar(BrowserWindowInterface* browser) {
  if (!browser || !browser->GetProfile() ||
      !browser->GetProfile()->IsRegularProfile()) {
    return;
  }
  if (GrokWebBarCoordinator::Get(browser->GetUnownedUserDataHost()))
    return;
  new GrokWebBarCoordinator(browser);
}

}  // namespace grok_companion