// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/browser/grok_companion/grok_web_bar.h"

#include <memory>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/agent_gateway/agent_gateway.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/views/side_panel/side_panel_web_ui_view.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "url/gurl.h"

namespace grok_companion {
namespace {

int GatewayPort() {
  if (auto* gw = agent_gateway::AgentGateway::GetInstance())
    return gw->port();
  base::FilePath home;
  if (base::PathService::Get(base::DIR_HOME, &home)) {
    std::string json;
    if (base::ReadFileToString(home.AppendASCII(".aether/gateway.json"),
                               &json)) {
      if (auto parsed = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC)) {
        if (std::optional<int> port = parsed->FindInt("port"))
          return *port;
      }
    }
  }
  return 9334;
}

GURL MakeURL(const char* path) {
  return GURL(std::string("http://") + kCompanionHost + ":" +
              base::NumberToString(GatewayPort()) + path);
}

base::FilePath GrokSettingsFile() {
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home))
    return base::FilePath();
  return home.AppendASCII(".aether/grok_settings.json");
}

base::DictValue LoadGrokSettings() {
  base::FilePath path = GrokSettingsFile();
  if (path.empty())
    return base::DictValue();
  std::string json;
  if (!base::ReadFileToString(path, &json))
    return base::DictValue();
  auto parsed = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  return parsed ? std::move(*parsed) : base::DictValue();
}

void SaveGrokSettings(const base::DictValue& settings) {
  base::FilePath path = GrokSettingsFile();
  if (path.empty())
    return;
  base::CreateDirectory(path.DirName());
  std::string json;
  if (base::JSONWriter::Write(settings, &json))
    base::WriteFile(path, json);
}

std::unique_ptr<views::View> CreateGrokCompanionView(
    BrowserWindowInterface* browser,
    Profile* profile,
    SidePanelEntryScope& scope,
    const GURL& url) {
  auto web_view = std::make_unique<views::WebView>(profile);
  web_view->SetID(SidePanelWebUIView::kSidePanelWebViewId);
  content::WebContents::CreateParams params(profile);
  auto contents = content::WebContents::Create(params);
  content::NavigationController::LoadURLParams load(url);
  load.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
  contents->GetController().LoadURLWithParams(load);
  web_view->SetOwnedWebContents(std::move(contents));
  web_view->SetPreferredSize(
      gfx::Size(SidePanelEntry::kSidePanelDefaultContentWidth, 0));
  return web_view;
}

}  // namespace

GURL GetCompanionURL() {
  return MakeURL(kCompanionPath);
}

GURL GetSearchURL() {
  return MakeURL(kSearchPath);
}

std::string GetSearchHomeMode() {
  base::DictValue settings = LoadGrokSettings();
  if (const std::string* mode = settings.FindString("search_home");
      mode && *mode == kSearchHomeWeb) {
    return kSearchHomeWeb;
  }
  return kSearchHomeBuild;
}

void SetSearchHomeMode(const std::string& mode) {
  base::DictValue settings = LoadGrokSettings();
  settings.Set("search_home",
               mode == kSearchHomeWeb ? kSearchHomeWeb : kSearchHomeBuild);
  SaveGrokSettings(settings);
}

GURL GetDefaultSearchHomeURL() {
  if (GetSearchHomeMode() == kSearchHomeWeb)
    return GURL(kGrokWebHomeURL);
  return GetSearchURL();
}

void RegisterGrokSidePanel(BrowserWindowInterface* browser) {
  if (!browser)
    return;
  RegisterGrokWebBar(browser);
  SidePanelRegistry* registry = SidePanelRegistry::From(browser);
  if (!registry)
    return;
  if (registry->GetEntryForKey(
          SidePanelEntry::Key(SidePanelEntry::Id::kSearchCompanion))) {
    return;
  }

  Profile* profile = browser->GetProfile();
  if (!profile || !profile->IsRegularProfile())
    return;

  const GURL companion_url = GetCompanionURL();
  auto entry = std::make_unique<SidePanelEntry>(
      SidePanelEntry::Key(SidePanelEntry::Id::kSearchCompanion),
      base::BindRepeating(
          [](BrowserWindowInterface* bwi, Profile* prof, GURL url,
             SidePanelEntryScope& scope) {
            return CreateGrokCompanionView(bwi, prof, scope, url);
          },
          browser, profile, companion_url),
      base::BindRepeating(
          []() { return SidePanelEntry::kSidePanelDefaultContentWidth; }));
  registry->Register(std::move(entry));
}

void OpenGrokSearchPage(BrowserWindowInterface* browser) {
  if (!browser)
    return;
  RegisterGrokWebBar(browser);
  tabs::TabInterface* tab = browser->GetActiveTabInterface();
  if (!tab)
    return;
  content::WebContents* contents = tab->GetContents();
  if (!contents)
    return;
  content::NavigationController::LoadURLParams params(GetDefaultSearchHomeURL());
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  contents->GetController().LoadURLWithParams(params);
}

void ToggleGrokSidePanel(BrowserWindowInterface* browser) {
  if (!browser)
    return;
  RegisterGrokSidePanel(browser);
  SidePanelUI* ui = browser->GetFeatures().side_panel_ui();
  if (!ui)
    return;
  ui->Toggle(SidePanelEntry::Key(SidePanelEntry::Id::kSearchCompanion),
             SidePanelOpenTrigger::kToolbarButton);
}

}  // namespace grok_companion