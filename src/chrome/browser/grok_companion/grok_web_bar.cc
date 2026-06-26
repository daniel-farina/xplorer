// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/grok_companion/grok_web_bar.h"

#include <memory>
#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/agent_gateway/agent_gateway.h"
#include "chrome/browser/agent_gateway/grok_native.h"
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


// The companion UI directory is resolved once, in the agent_gateway layer
// (agent_gateway::CompanionUiDir), and shared with the gateway's own static
// file server so the native overlay and the companion pages always read the
// same files. We bake the toolbar HTML/CSS/JS into the isolated-world injection
// (read live from disk on every injection, so edits go live without a rebuild)
// rather than fetching them from the page, because third-party CSP (connect-src)
// blocks a cross-origin fetch to the loopback gateway.
std::string ReadCompanionUiFile(const char* name) {
  base::FilePath file = agent_gateway::CompanionUiDir().AppendASCII(name);
  std::string contents;
  if (base::ReadFileToString(file, &contents) && !contents.empty())
    return contents;
  return std::string();
}

std::string LoadToolbarCss() {
  return ReadCompanionUiFile("toolbar.css");
}

std::string LoadToolbarHtml() {
  return ReadCompanionUiFile("toolbar.html");
}

std::string LoadToolbarJs() {
  return ReadCompanionUiFile("toolbar.js");
}
std::string JsonStringLiteral(const std::string& value) {
  std::string json;
  base::JSONWriter::Write(base::Value(value), &json);
  return json;
}

// Read the "toolbar" config dict from grok_settings.json and serialize it back
// to a compact JSON string. Returns "" if the file is missing, unparseable, or
// has no "toolbar" key -- callers then bake `toolbarConfig:null`. The native
// overlay can't fetch cross-origin (third-party CSP), so the config MUST be
// baked into the injection rather than fetched at runtime.
std::string LoadToolbarConfigJson() {
  base::FilePath path = ResolveDataFile("grok_settings.json");
  if (path.empty())
    return std::string();
  std::string contents;
  if (!base::ReadFileToString(path, &contents) || contents.empty())
    return std::string();
  auto parsed = base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
  if (!parsed)
    return std::string();
  const base::DictValue* toolbar = parsed->FindDict("toolbar");
  if (!toolbar)
    return std::string();
  std::string out;
  if (!base::JSONWriter::Write(*toolbar, &out))
    return std::string();
  return out;
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

// Emergency fallback: the bundled UI could not be read from disk (broken
// install). Inject a tiny static bar that links back to Xplorer -- deliberately
// minimal, never a second full toolbar that could drift from the canonical
// companion/ui/toolbar.html.
std::string MinimalFallbackScript(const std::string& gateway_origin) {
  return base::StringPrintf(
      R"((function(){if(!document.documentElement)return;var ID='xplorer-grok-bar';if(document.getElementById(ID))return;var b=document.createElement('header');b.id=ID;b.setAttribute('style','position:fixed;top:0;left:0;right:0;z-index:2147483647;display:flex;align-items:center;height:44px;padding:0 14px;background:#161616;color:#f2f2f2;font:600 14px -apple-system,system-ui,sans-serif;border-bottom:1px solid #333');var a=document.createElement('a');a.href=%s+'/search';a.textContent='Xplorer';a.setAttribute('style','color:#f2f2f2;text-decoration:none');b.appendChild(a);document.documentElement.insertBefore(b,document.documentElement.firstChild);document.documentElement.style.setProperty('padding-top','44px','important');})();)",
      JsonStringLiteral(gateway_origin).c_str());
}

std::string BuildInjectScript(const std::string& active_mode) {
  const std::string js = LoadToolbarJs();
  const std::string html = LoadToolbarHtml();
  const std::string css = LoadToolbarCss();
  const std::string gw =
      base::StringPrintf("http://%s:%d", kCompanionHost, GatewayPort());

  // If the bundled UI can't be read, degrade to a tiny static bar rather than a
  // full second toolbar that could drift from the canonical one.
  if (js.empty() || html.empty() || css.empty())
    return MinimalFallbackScript(gw);

  // Config-driven toolbar customization, baked in (the native overlay can't
  // fetch the gateway cross-origin). |tbcfg_js| is a JS expression evaluating
  // to either the parsed config object or null. We build it from a JSON string
  // literal so the renderer JSON.parse()s our trusted config rather than us
  // splicing raw JSON into source. JSON of our config contains no '%', but we
  // still append it via std::string concatenation (not StringPrintf) to be safe.
  const std::string toolbar_json = LoadToolbarConfigJson();
  const std::string tbcfg_js =
      toolbar_json.empty()
          ? std::string("null")
          : ("JSON.parse(" + JsonStringLiteral(toolbar_json) + ")");

  // companion/ui/toolbar.js is the single source of toolbar behavior for BOTH
  // surfaces. It defines window.XplorerToolbar; the bootstrap below calls
  // XplorerToolbar.mountNative() with the canonical markup + CSS baked in (so
  // the native overlay never fetches cross-origin). The markup's root-relative
  // hrefs are rewritten to absolute gateway URLs in JS at mount time (see
  // absolutizeHrefs() in toolbar.js) -- replacing the old C++ string rewrite.
  // NOTE: the mountNative({...}) object is left UNCLOSED here on purpose --
  // |toolbarConfig:| + tbcfg_js and the closing "});}})();" are appended below
  // via concatenation so the (possibly large) baked JS never hits StringPrintf.
  const std::string boot = base::StringPrintf(
      ";(function(){if(window.XplorerToolbar&&window.XplorerToolbar.mountNative)"
      "{window.XplorerToolbar.mountNative({surface:'native',baseHtml:%s,"
      "baseCss:%s,gatewayOrigin:%s,theme:%s,fallbackPill:%s",
      JsonStringLiteral(html).c_str(), JsonStringLiteral(css).c_str(),
      JsonStringLiteral(gw).c_str(),
      JsonStringLiteral(BrowserThemeAttribute()).c_str(),
      JsonStringLiteral(active_mode).c_str());

  const std::string boot_full =
      boot + ",toolbarConfig:" + tbcfg_js + "});}})();";

  // Concatenate the JS source -- never run it through a printf format string,
  // since it may contain '%'.
  return js + "\n" + boot_full;
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

  bool ShouldInject(content::WebContents* /*contents*/) const {
    // XPLORER: the injected in-page web bar is disabled. Navigation now lives in
    // the native "Bookmarks" tab group (seeded by AgentTabGrouper), so the
    // in-page injection on x.com/grok.com/grokipedia is suppressed.
    return false;
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