// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/grok_companion/grok_web_bar.h"

#include <memory>
#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/agent_gateway/agent_gateway.h"
#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

namespace grok_companion {
namespace {

constexpr int kMaxInjectAttempts = 40;

bool IsToolbarOverlayHost(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS())
    return false;
  const std::string_view host = url.host();
  if (host == "grok.com" || host == "www.grok.com" ||
      base::EndsWith(host, ".grok.com")) {
    return true;
  }
  return host == "grokipedia.com" || host == "www.grokipedia.com" ||
         base::EndsWith(host, ".grokipedia.com");
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

base::FilePath CompanionUiDir() {
  const char* env = getenv("XBROWSER_COMPANION_UI");
  if (env && *env)
    return base::FilePath(env);
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home))
    return base::FilePath();
  static constexpr const char* kCandidates[] = {
      "cli_experiment/aether/companion/ui",
      ".aether/companion/ui",
  };
  for (const char* rel : kCandidates) {
    base::FilePath candidate = home.AppendASCII(rel);
    if (base::DirectoryExists(candidate))
      return candidate;
  }
  return base::FilePath();
}

constexpr char kToolbarCssFallback[] =
    "#xbrowser-grok-bar.grok-toolbar{position:fixed;top:0;left:0;right:0;"
    "z-index:2147483647;display:flex;align-items:center;gap:12px;flex-wrap:"
    "nowrap;min-height:44px;box-sizing:border-box;padding:10px 16px;"
    "font:13px/1.4 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-"
    "serif;background:#161616;color:#f2f2f2;border-bottom:1px solid "
    "#333;box-shadow:0 1px 0 rgba(0,0,0,.25)}"
    ".grok-toolbar-spacer{flex:1;min-width:8px}"
    ".grok-logo{font-weight:700;font-size:16px;color:#f2f2f2;text-decoration:"
    "none;white-space:nowrap}"
    ".grok-modes{display:inline-flex;gap:2px;flex-shrink:0}"
    ".grok-mode{color:#aaa;padding:5px 12px;border-radius:999px;text-"
    "decoration:none;font-size:12px;white-space:nowrap}"
    ".grok-mode:hover{color:#f2f2f2;background:#222}"
    ".grok-toolbar-actions{display:inline-flex;align-items:center;gap:8px;"
    "flex-shrink:0}"
    ".grok-nav-pills{display:inline-flex;border:1px solid #333;border-radius:"
    "999px;background:#222;overflow:visible}"
    ".grok-pill-wrap{position:relative;display:flex}"
    ".grok-pill-wrap+.grok-pill-wrap .grok-pill{border-left:1px solid #333}"
    ".grok-pill{color:#aaa;padding:5px 12px;font-size:12px;text-decoration:"
    "none;white-space:nowrap;display:inline-flex;align-items:center}"
    ".grok-pill:hover,.grok-pill-wrap:hover>.grok-pill{color:#f2f2f2;"
    "background:#1a1a1a}"
    ".grok-pill.active{background:#2a2a2a;color:#f2f2f2;font-weight:500}"
    ".grok-pill-menu{display:none;position:absolute;top:calc(100% + 6px);"
    "right:0;min-width:148px;padding:4px;border:1px solid #333;border-radius:"
    "10px;background:#161616;box-shadow:0 6px 20px rgba(0,0,0,.35);"
    "flex-direction:column;z-index:200}"
    ".grok-pill-wrap:hover .grok-pill-menu{display:flex}"
    ".grok-pill-menu a{display:block;padding:7px 10px;border-radius:7px;"
    "color:#f2f2f2;text-decoration:none;font-size:12px}"
    ".grok-pill-menu a:hover{background:#222;color:#fff}";

std::string LoadToolbarCss() {
  base::FilePath css_file = CompanionUiDir().AppendASCII("toolbar.css");
  std::string css;
  if (base::ReadFileToString(css_file, &css) && !css.empty())
    return css;
  return kToolbarCssFallback;
}

std::string JsonStringLiteral(const std::string& value) {
  std::string json;
  base::JSONWriter::Write(base::Value(value), &json);
  return json;
}

std::string BrowserThemeAttribute() {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  if (!profile)
    return "";
  ThemeService* theme = ThemeServiceFactory::GetForProfile(profile);
  if (!theme)
    return "";
  switch (theme->GetBrowserColorScheme()) {
    case ThemeService::BrowserColorScheme::kDark:
      return "dark";
    case ThemeService::BrowserColorScheme::kLight:
      return "light";
    default:
      return "";
  }
}

std::string BuildInjectScript(const std::string& active_mode) {
  const std::string build_href = SwitchHomeURL(kSearchHomeBuild).spec();
  const std::string web_href = SwitchHomeURL(kSearchHomeWeb).spec();
  const std::string wiki_href = SwitchHomeURL(kSearchHomeWiki).spec();
  const std::string search_href = SearchPageURL("").spec();
  const std::string chat_href =
      base::StringPrintf("http://%s:%d/", kCompanionHost, GatewayPort());
  const std::string build_active =
      active_mode == kSearchHomeBuild ? " active" : "";
  const std::string web_active = active_mode == kSearchHomeWeb ? " active" : "";
  const std::string wiki_active =
      active_mode == kSearchHomeWiki ? " active" : "";

  const std::string toolbar_css = LoadToolbarCss();
  const std::string css_json = JsonStringLiteral(toolbar_css);

  const std::string html = base::StringPrintf(
      R"(<a class="grok-logo" href="%s">&#10022; Grok</a>)"
      R"(<div class="grok-toolbar-spacer"></div>)"
      R"(<div class="grok-toolbar-actions">)"
      R"(<div class="grok-nav-pills">)"
      R"(<div class="grok-pill-wrap">)"
      R"(<a class="grok-pill" href="https://x.com/i/chat" target="_blank" rel="noopener noreferrer">X Chat</a>)"
      R"(<div class="grok-pill-menu"><a href="https://x.com/i/chat" target="_blank" rel="noopener noreferrer">Open X Chat</a></div></div>)"
      R"(<div class="grok-pill-wrap">)"
      R"(<a class="grok-pill%s" href="%s">Grok Build</a>)"
      R"(<div class="grok-pill-menu"><a href="%s">Conversations</a></div></div>)"
      R"(<div class="grok-pill-wrap">)"
      R"(<a class="grok-pill%s" href="%s">Grok Web</a>)"
      R"(<div class="grok-pill-menu"><a href="https://grok.com/imagine" target="_blank" rel="noopener noreferrer">Imagine</a></div></div>)"
      R"(<div class="grok-pill-wrap">)"
      R"(<a class="grok-pill%s" href="%s">Wiki</a>)"
      R"(<div class="grok-pill-menu"><a href="https://grokipedia.com/" target="_blank" rel="noopener noreferrer">Grokipedia</a></div></div>)"
      R"(<div class="grok-pill-wrap">)"
      R"(<a class="grok-pill" href="https://x.com/" target="_blank" rel="noopener noreferrer">x.com</a>)"
      R"(<div class="grok-pill-menu"><a href="https://x.com/" target="_blank" rel="noopener noreferrer">Home</a></div></div>)"
      R"(</div></div>)",
      search_href.c_str(), build_active.c_str(), build_href.c_str(),
      chat_href.c_str(), web_active.c_str(), web_href.c_str(),
      wiki_active.c_str(), wiki_href.c_str());
  const std::string html_json = JsonStringLiteral(html);

  const std::string theme = BrowserThemeAttribute();
  const std::string theme_boot =
      theme.empty()
          ? ""
          : base::StringPrintf(
                "function applyTheme(){document.documentElement.setAttribute("
                "'data-theme','%s');}",
                theme.c_str());
  const std::string theme_call = theme.empty() ? "" : "applyTheme();";

  return base::StringPrintf(
      R"((function(){
  if(!document.documentElement)return;
  var BAR_ID='xbrowser-grok-bar',STYLE_ID='xbrowser-grok-toolbar-style';
  var CSS=%s,HTML=%s;
  %s
  function ensureStyle(){
    if(document.getElementById(STYLE_ID))return;
    var style=document.createElement('style');
    style.id=STYLE_ID;
    style.textContent=CSS;
    document.documentElement.appendChild(style);
  }
  function applyPadding(bar){
    var h=bar.getBoundingClientRect().height||44;
    var pad=h+'px';
    document.documentElement.style.setProperty('padding-top',pad,'important');
    if(document.body)document.body.style.setProperty('padding-top',pad,'important');
  }
  function mountBar(bar){
    var html=document.documentElement;
    if(bar.parentNode!==html) html.insertBefore(bar,html.firstChild);
    else if(html.firstChild!==bar) html.insertBefore(bar,html.firstChild);
  }
  function ensureBar(){
    ensureStyle();
    %s
    var bar=document.getElementById(BAR_ID);
    if(!bar){
      bar=document.createElement('header');
      bar.id=BAR_ID;
      bar.className='grok-toolbar';
    }
    bar.innerHTML=HTML;
    mountBar(bar);
    applyPadding(bar);
  }
  function barNeedsMount(){
    var bar=document.getElementById(BAR_ID);
    return !bar||bar.parentNode!==document.documentElement||
      document.documentElement.firstChild!==bar;
  }
  ensureBar();
  if(!window.__xbrowserGrokBarWatch){
    window.__xbrowserGrokBarWatch=true;
    new MutationObserver(function(){
      if(barNeedsMount()) ensureBar();
    }).observe(document.documentElement,{childList:true,subtree:true});
    setInterval(function(){
      if(barNeedsMount()) ensureBar();
    },1500);
  }
})();)",
      css_json.c_str(), html_json.c_str(), theme_boot.c_str(),
      theme_call.c_str());
}

class GrokWebBarInjector : public content::WebContentsObserver,
                           public content::WebContentsUserData<GrokWebBarInjector> {
 public:
  ~GrokWebBarInjector() override = default;

  static void Attach(content::WebContents* contents) {
    if (!contents)
      return;
    Profile* profile =
        Profile::FromBrowserContext(contents->GetBrowserContext());
    if (!profile || !profile->IsRegularProfile())
      return;
    if (!FromWebContents(contents))
      CreateForWebContents(contents);
  }

  void RefreshInjection() {
    RedirectLegacyNewTabIfNeeded(web_contents());
    ScheduleInject();
  }

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!navigation_handle->IsInPrimaryMainFrame() ||
        !navigation_handle->HasCommitted() ||
        navigation_handle->IsErrorPage()) {
      return;
    }
    if (IsLegacyChromeNewTab(navigation_handle->GetURL())) {
      RedirectLegacyNewTabIfNeeded(web_contents());
      return;
    }
    if (!IsToolbarOverlayHost(navigation_handle->GetURL()))
      return;
    ScheduleInject();
  }

  void DocumentOnLoadCompletedInPrimaryMainFrame() override {
    ScheduleInject();
  }

  void DOMContentLoaded(content::RenderFrameHost* render_frame_host) override {
    if (render_frame_host != web_contents()->GetPrimaryMainFrame())
      return;
    ScheduleInject();
  }

  void DidFinishLoad(content::RenderFrameHost* render_frame_host,
                     const GURL& validated_url) override {
    if (render_frame_host != web_contents()->GetPrimaryMainFrame())
      return;
    if (!IsToolbarOverlayHost(validated_url))
      return;
    ScheduleInject();
  }

  void DidStopLoading() override { ScheduleInject(); }

 private:
  friend class content::WebContentsUserData<GrokWebBarInjector>;

  explicit GrokWebBarInjector(content::WebContents* contents)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<GrokWebBarInjector>(*contents) {
    RedirectLegacyNewTabIfNeeded(contents);
    ScheduleInject();
    ScheduleStartupBurst();
  }

  bool ShouldInject(content::WebContents* contents) const {
    if (!contents)
      return false;
    if (IsToolbarOverlayHost(contents->GetLastCommittedURL()))
      return true;
    return IsToolbarOverlayHost(contents->GetVisibleURL());
  }

  void ScheduleInject() {
    content::WebContents* contents = web_contents();
    if (!ShouldInject(contents))
      return;
    inject_attempts_ = 0;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&GrokWebBarInjector::MaybeInject,
                                  weak_factory_.GetWeakPtr()));
  }

  void ScheduleStartupBurst() {
    static constexpr int kBurstDelaysMs[] = {100,  400,  1000, 2000,
                                             4000, 8000, 15000};
    for (int delay_ms : kBurstDelaysMs) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&GrokWebBarInjector::BurstInject,
                         weak_factory_.GetWeakPtr()),
          base::Milliseconds(delay_ms));
    }
  }

  void BurstInject() {
    if (!ShouldInject(web_contents()))
      return;
    MaybeInject();
  }

  void MaybeInject() {
    content::WebContents* contents = web_contents();
    if (!contents || !ShouldInject(contents))
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
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
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

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(GrokWebBarInjector);

}  // namespace

void AttachGrokWebBarInjector(content::WebContents* contents) {
  GrokWebBarInjector::Attach(contents);
  if (auto* injector = GrokWebBarInjector::FromWebContents(contents))
    injector->RefreshInjection();
}

void RegisterGrokWebBar(BrowserWindowInterface* browser) {
  if (!browser || !browser->GetProfile() ||
      !browser->GetProfile()->IsRegularProfile()) {
    return;
  }
  TabStripModel* model = browser->GetTabStripModel();
  if (!model)
    return;
  for (int i = 0; i < model->count(); ++i)
    AttachGrokWebBarInjector(model->GetWebContentsAt(i));
}

}  // namespace grok_companion