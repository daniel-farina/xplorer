// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/grok_companion/grok_web_bar.h"

#include <map>
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

base::FilePath CompanionUiDir() {
  const char* env = getenv("XBROWSER_COMPANION_UI");
  if (env && *env)
    return base::FilePath(env);
  base::FilePath home;
  if (base::PathService::Get(base::DIR_HOME, &home)) {
    base::FilePath candidate =
        home.AppendASCII("cli_experiment/aether/companion/ui");
    if (base::DirectoryExists(candidate))
      return candidate;
  }
  return base::FilePath();
}

std::string LoadToolbarCss() {
  base::FilePath css_file = CompanionUiDir().AppendASCII("toolbar.css");
  std::string css;
  if (!base::ReadFileToString(css_file, &css))
    return "";
  return css;
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
  const std::string search_href = SearchPageURL("").spec();
  const std::string images_href = SearchPageURL("images").spec();
  const std::string videos_href = SearchPageURL("videos").spec();
  const std::string imagine_href = SearchPageURL("imagine").spec();
  const std::string build_active =
      active_mode == kSearchHomeBuild ? " active" : "";
  const std::string web_active = active_mode == kSearchHomeWeb ? " active" : "";

  const std::string toolbar_css = LoadToolbarCss();
  const std::string css_json = JsonStringLiteral(toolbar_css);

  const std::string html = base::StringPrintf(
      R"(<a class="grok-logo" href="%s">&#10022; Grok</a>)"
      R"(<nav class="grok-modes">)"
      R"(<a class="grok-mode" href="%s">All</a>)"
      R"(<a class="grok-mode" href="%s">Images</a>)"
      R"(<a class="grok-mode" href="%s">Videos</a>)"
      R"(<a class="grok-mode" href="%s">Imagine</a>)"
      R"(</nav>)"
      R"(<div class="grok-toolbar-spacer"></div>)"
      R"(<div class="grok-toolbar-actions">)"
      R"(<div class="grok-toggle">)"
      R"(<a class="grok-toggle-opt%s" href="%s">Grok Build</a>)"
      R"(<a class="grok-toggle-opt%s" href="%s">Grok Web</a>)"
      R"(</div></div>)",
      search_href.c_str(), search_href.c_str(), images_href.c_str(),
      videos_href.c_str(), imagine_href.c_str(), build_active.c_str(),
      build_href.c_str(), web_active.c_str(), web_href.c_str());
  const std::string html_json = JsonStringLiteral(html);

  const std::string theme = BrowserThemeAttribute();
  const std::string theme_js =
      theme.empty()
          ? ""
          : base::StringPrintf(
                "document.documentElement.setAttribute('data-theme','%s');",
                theme.c_str());

  return base::StringPrintf(
      R"((function(){
  if (document.getElementById('xbrowser-grok-bar')) return;
  %s
  var style = document.createElement('style');
  style.textContent = %s;
  document.documentElement.appendChild(style);
  var bar = document.createElement('header');
  bar.id = 'xbrowser-grok-bar';
  bar.className = 'grok-toolbar';
  bar.innerHTML = %s;
  var root = document.body || document.documentElement;
  root.insertBefore(bar, root.firstChild);
  var h = bar.getBoundingClientRect().height || 44;
  var pad = h + 'px';
  document.documentElement.style.setProperty('padding-top', pad, 'important');
  if (document.body) document.body.style.setProperty('padding-top', pad, 'important');
})();)",
      theme_js.c_str(), css_json.c_str(), html_json.c_str());
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