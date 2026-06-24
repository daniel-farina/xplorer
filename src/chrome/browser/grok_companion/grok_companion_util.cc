// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/browser/grok_companion/grok_fab.h"
#include "chrome/browser/grok_companion/grok_web_bar.h"

#include <memory>
#include <utility>

#include "base/callback_list.h"
#include "base/files/file_util.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/agent_gateway/agent_gateway.h"
#include "chrome/browser/agent_gateway/xplorer_paths.h"
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
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"  // XPLORER
#include "content/public/browser/web_contents_delegate.h"  // XPLORER
#include "components/input/native_web_keyboard_event.h"  // XPLORER
#include "base/memory/raw_ptr.h"  // XPLORER
#include "url/gurl.h"

namespace grok_companion {
namespace {

constexpr char kGrokSettingsFile[] = "grok_settings.json";
constexpr char kGatewayFile[] = "gateway.json";

int GatewayPort() {
  if (auto* gw = agent_gateway::AgentGateway::GetInstance())
    return gw->port();
  base::FilePath gateway = xplorer_paths::Resolve(kGatewayFile);
  if (!gateway.empty()) {
    std::string json;
    if (base::ReadFileToString(gateway, &json)) {
      if (auto parsed = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC)) {
        if (std::optional<int> port = parsed->FindInt("port"))
          return *port;
      }
    }
  }
  return kCompanionPort;
}

GURL MakeURL(const char* path) {
  return GURL(std::string("http://") + kCompanionHost + ":" +
              base::NumberToString(GatewayPort()) + path);
}

base::FilePath GrokSettingsFile() {
  return xplorer_paths::Resolve(kGrokSettingsFile);
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

// The side-panel companion hosts an http page in a raw views::WebView.
//
// macOS clipboard root cause: on Mac, ⌘C/⌘V/⌘X (and the Edit-menu Cut/Copy/
// Paste items) are plain Cocoa selectors `cut:`/`copy:`/`paste:` dispatched to
// the *NSWindow first responder* — via `[NSApp sendAction:@selector(paste:)
// to:nil]` in remote_cocoa::ApplicationBridge::ForwardCutCopyPasteToNSApp(),
// reached from BrowserView::CutCopyPaste(). They are NOT routed by TabStripModel
// membership or the Browser/WebContents delegate. RenderWidgetHostViewCocoa's
// -performKeyEquivalent: and the menu's sendAction both require
// `[[self window] firstResponder] == self`
// (content/app_shim_remote_cocoa/render_widget_host_view_cocoa.mm).
//
// A standalone WebContents that is never WasShown() keeps its
// RenderWidgetHostViewCocoa NSView created with `hidden = YES`
// (render_widget_host_ns_view_bridge.mm), and AppKit refuses to make a hidden
// view the first responder. Plain typing still works (key events are routed via
// the renderer-host focus path, which does not require AppKit first-responder
// status), but command-key equivalents and Edit-menu actions never reach the
// view — exactly the reported symptom.
//
// The fix mirrors what every WebUI side panel gets for free via
// SidePanelWebUIView::ViewHierarchyChanged(): drive the contents to the shown/
// visible state once it is in the widget hierarchy (so SetVisible(true) ->
// cocoa_view_.hidden = NO), then focus it so its RWHV becomes first responder.
// This is cross-platform safe; WasShown()/RequestFocus() are no-ops or correct
// on Win/Linux. We also keep a delegate that forwards unhandled keys to the
// focus manager so browser accelerators keep working from the panel.
class CompanionWebView : public views::WebView {
 public:
  explicit CompanionWebView(Profile* profile)
      : views::WebView(profile), delegate_(this) {}
  CompanionWebView(const CompanionWebView&) = delete;
  CompanionWebView& operator=(const CompanionWebView&) = delete;
  ~CompanionWebView() override {
    if (web_contents())
      web_contents()->SetDelegate(nullptr);
  }

  void AttachContents(std::unique_ptr<content::WebContents> contents) {
    contents->SetDelegate(&delegate_);
    SetOwnedWebContents(std::move(contents));
  }

  // views::WebView:
  void ViewHierarchyChanged(
      const views::ViewHierarchyChangedDetails& details) override {
    views::WebView::ViewHierarchyChanged(details);
    // Once added to the widget, mark the contents shown so its
    // RenderWidgetHostView (and, on macOS, its RenderWidgetHostViewCocoa NSView)
    // becomes visible and thus eligible to be the NSWindow first responder. This
    // is what makes ⌘C/⌘V/⌘X reach the focused input. Mirrors
    // SidePanelWebUIView::ViewHierarchyChanged().
    if (details.is_add && details.child == this && web_contents()) {
      web_contents()->WasShown();
    }
  }

 private:
  class Delegate : public content::WebContentsDelegate {
   public:
    explicit Delegate(CompanionWebView* owner) : owner_(owner) {}
    bool HandleKeyboardEvent(
        content::WebContents* source,
        const input::NativeWebKeyboardEvent& event) override {
      return handler_.HandleKeyboardEvent(event, owner_->GetFocusManager());
    }

   private:
    const raw_ptr<CompanionWebView> owner_;
    views::UnhandledKeyboardEventHandler handler_;
  };

  Delegate delegate_;
};

std::unique_ptr<views::View> CreateGrokCompanionView(
    BrowserWindowInterface* browser,
    Profile* profile,
    SidePanelEntryScope& scope,
    const GURL& url) {
  auto web_view = std::make_unique<CompanionWebView>(profile);
  web_view->SetID(SidePanelWebUIView::kSidePanelWebViewId);
  content::WebContents::CreateParams params(profile);
  auto contents = content::WebContents::Create(params);
  content::NavigationController::LoadURLParams load(url);
  load.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
  contents->GetController().LoadURLWithParams(load);
  web_view->AttachContents(std::move(contents));
  web_view->SetPreferredSize(
      gfx::Size(SidePanelEntry::kSidePanelDefaultContentWidth, 0));
  return web_view;
}

// Subscribers (live XplorerToolbarViews) notified when toolbar config changes.
base::RepeatingClosureList& ToolbarConfigChangedCallbacks() {
  static base::NoDestructor<base::RepeatingClosureList> list;
  return *list;
}

}  // namespace

base::FilePath GetXplorerDataDir() {
  return xplorer_paths::DataDir();
}

base::FilePath ResolveDataFile(const char* filename) {
  return xplorer_paths::Resolve(filename);
}

GURL GetCompanionURL() {
  return MakeURL(kCompanionPath);
}

GURL GetSearchURL() {
  return MakeURL(kSearchPath);
}

GURL GetWelcomeURL() {
  return MakeURL(kWelcomePath);
}

bool HasCompletedWelcome() {
  base::DictValue settings = LoadGrokSettings();
  return settings.FindBool("welcome_completed").value_or(false);
}

void MarkWelcomeCompleted() {
  base::DictValue settings = LoadGrokSettings();
  settings.Set("welcome_completed", true);
  SaveGrokSettings(settings);
}

GURL GetStartupHomeURL() {
  if (!HasCompletedWelcome())
    return GetWelcomeURL();
  return GetDefaultSearchHomeURL();
}

bool IsGrokHomeURL(const GURL& url) {
  if (!url.is_valid())
    return false;
  // Match origin + path against the gateway home pages, ignoring query/ref so
  // e.g. /search?q=... still counts as the home.
  GURL::Replacements clear;
  clear.ClearQuery();
  clear.ClearRef();
  const GURL bare = url.ReplaceComponents(clear);
  return bare == GetSearchURL() || bare == GetCompanionURL() ||
         bare == GetWelcomeURL();
}

std::string GetSearchHomeMode() {
  base::DictValue settings = LoadGrokSettings();
  if (const std::string* mode = settings.FindString("search_home")) {
    if (*mode == kSearchHomeWeb)
      return kSearchHomeWeb;
    if (*mode == kSearchHomeWiki)
      return kSearchHomeWiki;
  }
  return kSearchHomeWeb;
}

void SetSearchHomeMode(const std::string& mode) {
  base::DictValue settings = LoadGrokSettings();
  std::string saved = kSearchHomeBuild;
  if (mode == kSearchHomeWeb)
    saved = kSearchHomeWeb;
  else if (mode == kSearchHomeWiki)
    saved = kSearchHomeWiki;
  settings.Set("search_home", saved);
  SaveGrokSettings(settings);
}

std::vector<base::DictValue> GetToolbarPillConfigs() {
  std::vector<base::DictValue> pills;
  base::DictValue settings = LoadGrokSettings();
  const base::DictValue* toolbar = settings.FindDict("toolbar");
  if (!toolbar)
    return pills;
  const base::ListValue* list = toolbar->FindList("pills");
  if (!list)
    return pills;
  for (const base::Value& entry : *list) {
    if (!entry.is_dict())
      continue;
    pills.push_back(entry.GetDict().Clone());
  }
  return pills;
}

void SetToolbarPillConfigs(const std::vector<base::DictValue>& pills) {
  base::DictValue settings = LoadGrokSettings();
  base::ListValue list;
  for (const base::DictValue& pill : pills)
    list.Append(pill.Clone());
  settings.EnsureDict("toolbar")->Set("pills", std::move(list));
  SaveGrokSettings(settings);
  NotifyToolbarConfigChanged();
}

base::CallbackListSubscription AddToolbarConfigChangedCallback(
    base::RepeatingClosure callback) {
  return ToolbarConfigChangedCallbacks().Add(std::move(callback));
}

void NotifyToolbarConfigChanged() {
  ToolbarConfigChangedCallbacks().Notify();
}

GURL GetDefaultSearchHomeURL() {
  const std::string home = GetSearchHomeMode();
  if (home == kSearchHomeWeb)
    return GetSearchURL();
  if (home == kSearchHomeWiki)
    return GURL(kGrokWikiHomeURL);
  return GetCompanionURL();
}

bool IsLegacyChromeNewTab(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIs("chrome"))
    return false;
  const std::string_view host = url.host();
  return host == chrome::kChromeUINewTabHost ||
         host == chrome::kChromeUINewTabPageHost ||
         host == chrome::kChromeUINewTabPageThirdPartyHost;
}

void RedirectLegacyNewTabIfNeeded(content::WebContents* contents) {
  if (!contents)
    return;
  GURL url = contents->GetLastCommittedURL();
  if (url.is_empty() || url.IsAboutBlank())
    url = contents->GetVisibleURL();
  if (!IsLegacyChromeNewTab(url))
    return;
  content::NavigationController::LoadURLParams params(GetStartupHomeURL());
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  contents->GetController().LoadURLWithParams(params);
}

void RegisterGrokSidePanel(BrowserWindowInterface* browser) {
  if (!browser)
    return;
  RegisterGrokWebBar(browser);
  RegisterGrokFab(browser);
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
  RegisterGrokFab(browser);
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

void AskGrokAboutPage(content::WebContents* web_contents) {
  if (!web_contents)
    return;
  // Build a page-context prompt and hand it to the gateway /omnibox endpoint,
  // which stores it as a pending grok-web prompt and 302s to grok.com where the
  // injector auto-submits it. (Replaces Chrome's "Ask Google about this page".)
  std::string prompt = "Tell me about this page";
  std::u16string title = web_contents->GetTitle();
  if (!title.empty())
    prompt += ": " + base::UTF16ToUTF8(title);
  GURL page = web_contents->GetLastCommittedURL();
  if (page.is_valid() && page.SchemeIsHTTPOrHTTPS())
    prompt += " " + page.spec();
  GURL dest(std::string("http://") + kCompanionHost + ":" +
            base::NumberToString(GatewayPort()) + "/omnibox?q=" +
            base::EscapeQueryParamValue(prompt, /*use_plus=*/true));
  content::NavigationController::LoadURLParams params(dest);
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  web_contents->GetController().LoadURLWithParams(params);
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

void OpenGrokSidePanel(BrowserWindowInterface* browser) {
  if (!browser)
    return;
  RegisterGrokSidePanel(browser);
  SidePanelUI* ui = browser->GetFeatures().side_panel_ui();
  if (!ui)
    return;
  // Show() is idempotent-open: opens if closed, re-shows if already active —
  // unlike Toggle() it never closes the panel.
  ui->Show(SidePanelEntry::Key(SidePanelEntry::Id::kSearchCompanion),
           SidePanelOpenTrigger::kToolbarButton);
}

}  // namespace grok_companion