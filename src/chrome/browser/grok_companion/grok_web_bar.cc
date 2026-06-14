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
  return IsGrokToolbarOverlayHost(url);
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
  const char* env = getenv("XPLORER_COMPANION_UI");
  if (!env || !*env)
    env = getenv("XBROWSER_COMPANION_UI");
  if (env && *env)
    return base::FilePath(env);
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home))
    return base::FilePath();
  static constexpr const char* kCandidates[] = {
      "cli_experiment/aether/companion/ui",
      ".xplorer/companion/ui",
      ".xbrowser/companion/ui",
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
    "#xplorer-grok-bar.grok-toolbar{position:fixed;top:0;left:0;right:0;"
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
    ".grok-pill-wrap::after{content:'';position:absolute;left:0;right:0;"
    "top:100%;height:10px;z-index:199}"
    ".grok-pill-wrap+.grok-pill-wrap .grok-pill{border-left:1px solid #333}"
    ".grok-pill{color:#aaa;padding:5px 12px;font-size:12px;text-decoration:"
    "none;white-space:nowrap;display:inline-flex;align-items:center}"
    ".grok-pill:hover,.grok-pill-wrap:hover>.grok-pill{color:#f2f2f2;"
    "background:#1a1a1a}"
    ".grok-pill.active{background:#2a2a2a;color:#f2f2f2;font-weight:500}"
    ".grok-pill-menu{display:none;position:absolute;top:100%;margin-top:6px;"
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
  const std::string apps_href =
      base::StringPrintf("http://%s:%d/apps", kCompanionHost, GatewayPort());
  const std::string toolbar_css = LoadToolbarCss();
  const std::string css_json = JsonStringLiteral(toolbar_css);
  const std::string fallback_pill_json = JsonStringLiteral(active_mode);

  const std::string html = base::StringPrintf(
      R"(<a class="grok-logo" href="%s">&#10022; Grok</a>)"
      R"(<div class="grok-toolbar-spacer"></div>)"
      R"(<div class="grok-toolbar-actions">)"
      R"(<div class="grok-nav-pills">)"
      R"(<div class="grok-pill-wrap">)"
      R"(<a class="grok-pill" data-pill="xchat" href="https://x.com/i/chat" rel="noopener noreferrer">X Chat</a>)"
      R"(<div class="grok-pill-menu"><a href="https://x.com/i/chat" rel="noopener noreferrer">Open X Chat</a></div></div>)"
      R"(<div class="grok-pill-wrap">)"
      R"(<a class="grok-pill" data-pill="build" href="%s">Grok Build</a>)"
      R"(<div class="grok-pill-menu"><a href="%s">Conversations</a><a href="%s">Apps</a></div></div>)"
      R"(<div class="grok-pill-wrap">)"
      R"(<a class="grok-pill" data-pill="web" href="%s">Grok Web</a>)"
      R"(<div class="grok-pill-menu"><a href="https://grok.com/imagine" target="_blank" rel="noopener noreferrer">Imagine</a></div></div>)"
      R"(<div class="grok-pill-wrap">)"
      R"(<a class="grok-pill" data-pill="wiki" href="%s">Wiki</a>)"
      R"(<div class="grok-pill-menu"><a href="https://grokipedia.com/" target="_blank" rel="noopener noreferrer">Grokipedia</a></div></div>)"
      R"(<div class="grok-pill-wrap">)"
      R"(<a class="grok-pill" data-pill="xcom" href="https://x.com/" rel="noopener noreferrer">x.com</a>)"
      R"(<div class="grok-pill-menu"><a href="https://x.com/" rel="noopener noreferrer">Home</a></div></div>)"
      R"(</div></div>)",
      search_href.c_str(), build_href.c_str(), chat_href.c_str(),
      apps_href.c_str(), web_href.c_str(), wiki_href.c_str());
  const std::string html_json = JsonStringLiteral(html);
  const std::string gw =
      base::StringPrintf("http://%s:%d", kCompanionHost, GatewayPort());
  const std::string gw_json = JsonStringLiteral(gw);

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
  var BAR_ID='xplorer-grok-bar',STYLE_ID='xplorer-grok-toolbar-style';
  var CSS=%s,HTML=%s,FALLBACK_PILL=%s,GW=%s;
  %s
  function isXHost(host){
    return host==='x.com'||host.endsWith('.x.com')||
      host==='twitter.com'||host.endsWith('.twitter.com');
  }
  function activePillId(){
    var host=(location.hostname||'').toLowerCase();
    var path=(location.pathname||'').toLowerCase();
    if(host.indexOf('grok.com')>=0)return 'web';
    if(host.indexOf('grokipedia.com')>=0)return 'wiki';
    if(isXHost(host)){
      if(path==='/i/chat'||path.indexOf('/i/chat/')===0||
         path==='/messages'||path.indexOf('/messages/')===0)return 'xchat';
      return 'xcom';
    }
    if(host==='127.0.0.1'||host==='localhost'){
      if(path.indexOf('/search')===0)return 'web';
      if(path.indexOf('/apps')===0||path==='/'||path.indexOf('/app')===0)return 'build';
    }
    return FALLBACK_PILL||'';
  }
  function applyActivePill(){
    var bar=document.getElementById(BAR_ID);
    if(!bar)return;
    var id=activePillId();
    bar.querySelectorAll('.grok-pill[data-pill]').forEach(function(p){
      p.classList.toggle('active',!!id&&p.getAttribute('data-pill')===id);
    });
  }
  function hookHistory(){
    if(window.__xplorerGrokHistoryHooked)return;
    window.__xplorerGrokHistoryHooked=true;
    var push=history.pushState,replacement=history.replaceState;
    if(push)history.pushState=function(){
      var r=push.apply(this,arguments);applyActivePill();return r;
    };
    if(replacement)history.replaceState=function(){
      var r=replacement.apply(this,arguments);applyActivePill();return r;
    };
    window.addEventListener('popstate',applyActivePill);
  }
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
    applyActivePill();
    wirePillHandoffs(bar);
  }
  function barNeedsMount(){
    var bar=document.getElementById(BAR_ID);
    return !bar||bar.parentNode!==document.documentElement||
      document.documentElement.firstChild!==bar;
  }
  function onRouteChange(){
    applyActivePill();
  }
  function pageQuery(){
    try{
      var q=new URLSearchParams(location.search).get('q')||'';
      if(q)return q;
      try{return localStorage.getItem('xplorer_search_query')||'';}catch(e){return '';}
    }catch(e){return '';}
  }
  function pageSearchMode(){
    try{return localStorage.getItem('xplorer_search_mode')||'';}catch(e){return '';}
  }
  function handoffQuery(q,mode,fallback){
    var prompt=q;
    if(mode==='imagine')prompt='Generate an image: '+q;
    else if(mode==='videos')prompt='Search for videos: '+q;
    else if(mode==='images')prompt='Search for images: '+q;
    fetch(GW+'/api/page/grok-web',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({query:prompt})
    }).then(function(r){return r.json().then(function(d){
      if(!r.ok)throw new Error(d.error||'handoff failed');
      var url=d.grok_url||fallback;
      if(mode==='imagine'&&url.indexOf('xplorer_grok=')>=0){
        url=url.replace(/^https:\/\/grok\.com\/?/,'https://grok.com/imagine');
      }
      location.href=url;
    });}).catch(function(){location.href=fallback;});
  }
  function wirePillHandoffs(bar){
    if(!bar||bar.dataset.pillHandoff==='1')return;
    bar.dataset.pillHandoff='1';
    bar.addEventListener('click',function(ev){
      var imagineLink=ev.target&&ev.target.closest?
        ev.target.closest('a[href*="grok.com/imagine"]'):null;
      if(imagineLink){
        var iq=pageQuery();
        if(!iq)return;
        ev.preventDefault();
        ev.stopPropagation();
        handoffQuery(iq,'imagine','https://grok.com/imagine');
        return;
      }
      var pill=ev.target&&ev.target.closest?ev.target.closest('.grok-pill[data-pill]'):null;
      if(!pill||pill.getAttribute('data-pill')!=='web')return;
      var host=(location.hostname||'').toLowerCase();
      if(host.indexOf('grokipedia.com')<0)return;
      var q=pageQuery();
      if(!q)return;
      ev.preventDefault();
      ev.stopPropagation();
      handoffQuery(q,pageSearchMode(),'https://grok.com/');
    },true);
  }
  ensureBar();
  hookHistory();
  window.addEventListener('popstate',onRouteChange);
  window.addEventListener('pageshow',onRouteChange);
  document.addEventListener('visibilitychange',function(){
    if(!document.hidden) onRouteChange();
  });
  if(!window.__xplorerGrokBarWatch){
    window.__xplorerGrokBarWatch=true;
    var lastPath=location.pathname+location.search+location.hash;
    new MutationObserver(function(){
      if(barNeedsMount()) ensureBar();
      else{
        var p=location.pathname+location.search+location.hash;
        if(p!==lastPath){lastPath=p;onRouteChange();}
      }
    }).observe(document.documentElement,{childList:true,subtree:true});
    setInterval(function(){
      if(barNeedsMount()) ensureBar();
      else{
        var p=location.pathname+location.search+location.hash;
        if(p!==lastPath){lastPath=p;onRouteChange();}
      }
    },400);
  }
})();)",
      css_json.c_str(), html_json.c_str(), fallback_pill_json.c_str(),
      gw_json.c_str(), theme_boot.c_str(), theme_call.c_str());
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