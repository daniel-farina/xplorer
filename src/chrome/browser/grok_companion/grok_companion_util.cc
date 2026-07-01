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
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/base64.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/agent_gateway/agent_gateway.h"
#include "chrome/browser/agent_gateway/xplorer_paths.h"
#include "chrome/browser/image_editor/screenshot_flow.h"
#include "ui/gfx/image/image.h"
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
#include "base/memory/weak_ptr.h"  // XPLORER
#include "base/task/sequenced_task_runner.h"  // XPLORER
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
// macOS clipboard root cause: Cut/Copy/Paste/Select-All (⌘C/⌘V/⌘X/⌘A and the
// Edit menu) are Cocoa selectors dispatched to the NSWindow *first responder*
// (`[NSApp sendAction:@selector(paste:) to:nil]` from BrowserView::CutCopyPaste,
// and RenderWidgetHostViewCocoa -performKeyEquivalent: which requires
// `[[self window] firstResponder] == self`). They are NOT routed by the tab
// strip, the Browser, or the WebContentsDelegate — so the side panel's RWHV
// NSView must literally BE the window first responder.
//
// Two pieces were missing:
//  (a) WasShown() un-hides the RWHV NSView (cocoa_view_.hidden = NO) — necessary
//      (AppKit won't make a hidden view first responder) but NOT sufficient: it
//      never calls makeFirstResponder. Plain typing worked anyway via the
//      renderer page-focus path (no first-responder guard) — hence the symptom.
//  (b) The actual focus: web_contents()->Focus() -> RWHVMac::Focus() ->
//      MakeFirstResponder(). For a views::WebView the only entry is OnFocus(),
//      which fires on RequestFocus(). We drive it after the view is shown+drawn
//      (deferred + retried, since the side panel opens animated), and again on
//      OnFocus / OnWebContentsFocused so clicking back in re-acquires it.
// Also: SetDelegate() MUST run after SetOwnedWebContents() (which itself calls
// SetDelegate(this)), or the key-forwarding delegate is silently clobbered.
// Cross-platform safe — WasShown()/Focus() are correct no-ops on Win/Linux,
// where clipboard isn't first-responder-gated.
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
    // Delegate AFTER SetOwnedWebContents(): the latter calls
    // wc_owner_->SetDelegate(this), which would clobber a delegate set first.
    SetOwnedWebContents(std::move(contents));
    web_contents()->SetDelegate(&delegate_);
  }

  // views::WebView:
  void ViewHierarchyChanged(
      const views::ViewHierarchyChangedDetails& details) override {
    views::WebView::ViewHierarchyChanged(details);
    if (details.is_add && details.child == this && web_contents()) {
      web_contents()->WasShown();  // un-hide the RWHV NSView (precondition)
      FocusContentsSoon(8);        // then make it the window first responder
    }
  }

  void OnFocus() override {
    // Whenever this view is focused by any path, ensure the contents is shown
    // before it is focused (never ask a hidden NSView to be first responder).
    if (web_contents())
      web_contents()->WasShown();
    views::WebView::OnFocus();
  }

  void OnWebContentsFocused(
      content::RenderWidgetHost* render_widget_host) override {
    views::WebView::OnWebContentsFocused(render_widget_host);
    // Clicking back into the panel keeps this WebView views-focused so the RWHV
    // stays the window first responder and clipboard keeps working.
    RequestFocus();
  }

 private:
  void FocusContentsSoon(int tries) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&CompanionWebView::FocusContentsNow,
                                  weak_factory_.GetWeakPtr(), tries));
  }

  void FocusContentsNow(int tries) {
    if (!web_contents())
      return;
    // RequestFocus() only takes once the view is drawn (visible in the tree).
    // The side panel opens animated, so retry a few turns if not ready yet.
    if (!GetWidget() || !IsDrawn()) {
      if (tries > 0)
        FocusContentsSoon(tries - 1);
      return;
    }
    // Routes WebView::OnFocus() -> web_contents()->Focus() -> RWHVMac::Focus()
    // -> [window makeFirstResponder:cocoa_view_].
    RequestFocus();
  }

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
  base::WeakPtrFactory<CompanionWebView> weak_factory_{this};
};

// Weak handle to the most-recently-created side-panel companion WebContents, so
// OpenGrokSidePanelAt() can navigate the live panel to a specific path after
// showing it (the SidePanelEntry is registered once with a fixed "/" URL and
// reuses its view, so re-Show() does not re-navigate on its own). Cleared
// automatically when the contents is destroyed (raw WeakPtr). UI thread only.
base::WeakPtr<content::WebContents>& LiveCompanionContents() {
  static base::NoDestructor<base::WeakPtr<content::WebContents>> contents;
  return *contents;
}

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
  // Track this contents so a subsequent OpenGrokSidePanelAt() can navigate it.
  LiveCompanionContents() = contents->GetWeakPtr();
  web_view->AttachContents(std::move(contents));
  web_view->SetPreferredSize(
      gfx::Size(SidePanelEntry::kSidePanelDefaultContentWidth, 0));
  return web_view;
}

// Subscribers (live AgentTabGroupers) notified when the user-editable bookmark
// list changes, so open "Bookmarks" group tabs can be reconciled in place.
base::RepeatingClosureList& BookmarkConfigChangedCallbacks() {
  static base::NoDestructor<base::RepeatingClosureList> list;
  return *list;
}

}  // namespace

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
  // XPLORER: new tabs / startup / tab-restore always land on /search, regardless
  // of the Web/Wiki/Build toggle. GetDefaultSearchHomeURL() stays untouched
  // (shared with OpenGrokSearchPage + the omnibox/NTP chip, which honor the
  // toggle).
  return GetSearchURL();
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

std::vector<base::DictValue> GetBookmarkConfigs() {
  std::vector<base::DictValue> bookmarks;
  base::DictValue settings = LoadGrokSettings();
  const base::ListValue* list = settings.FindList("bookmarks");
  if (!list)
    return bookmarks;
  for (const base::Value& entry : *list) {
    if (!entry.is_dict())
      continue;
    bookmarks.push_back(entry.GetDict().Clone());
  }
  return bookmarks;
}

void SetBookmarkConfigs(const std::vector<base::DictValue>& bookmarks) {
  base::DictValue settings = LoadGrokSettings();
  base::ListValue list;
  for (const base::DictValue& bookmark : bookmarks)
    list.Append(bookmark.Clone());
  settings.Set("bookmarks", std::move(list));
  SaveGrokSettings(settings);
  NotifyBookmarkConfigChanged();
}

void RemoveBookmarkConfig(int64_t node_id) {
  std::vector<base::DictValue> bookmarks = GetBookmarkConfigs();
  bool removed = false;
  for (auto it = bookmarks.begin(); it != bookmarks.end();) {
    int64_t id = 0;
    const std::string* id_str = it->FindString("id");
    if (id_str && base::StringToInt64(*id_str, &id) && id == node_id) {
      it = bookmarks.erase(it);
      removed = true;
    } else {
      ++it;
    }
  }
  if (!removed)
    return;
  SetBookmarkConfigs(bookmarks);
}

base::CallbackListSubscription AddBookmarkConfigChangedCallback(
    base::RepeatingClosure callback) {
  return BookmarkConfigChangedCallbacks().Add(std::move(callback));
}

void NotifyBookmarkConfigChanged() {
  BookmarkConfigChangedCallbacks().Notify();
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

void OpenGrokSidePanelAt(BrowserWindowInterface* browser,
                         const std::string& path) {
  if (!browser)
    return;
  // Open (or re-show) the panel first; on a first open this synchronously
  // creates the CompanionWebView and records its contents in
  // LiveCompanionContents(). Then navigate that live contents to companion +
  // |path| — the same LoadURLWithParams pattern OpenGrokSearchPage /
  // AskGrokAboutPage use — so an already-open panel jumps to the new path too.
  OpenGrokSidePanel(browser);
  content::WebContents* contents = LiveCompanionContents().get();
  if (!contents)
    return;
  const GURL dest = GetCompanionURL().Resolve(path);
  if (!dest.is_valid())
    return;
  content::NavigationController::LoadURLParams params(dest);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
  contents->GetController().LoadURLWithParams(params);
}

namespace {
// Owns a ScreenshotFlow for one region drag-select, then self-deletes. On a
// successful selection it writes the region PNG (base64) to a one-shot pending
// file the sidebar reads via GET /api/pending-image, and opens the Grok side
// panel to run the vision search. On cancel/navigate it just cleans up.
class GrokRegionCapture {
 public:
  // One drag-select at a time: a rapid second trigger (double-clicked button /
  // menu while an overlay is up) would stack a second ScreenshotFlow overlay on
  // the tab. The flag flips back in OnCaptured (select/escape/navigate all land
  // there).
  static bool active_;

  GrokRegionCapture(BrowserWindowInterface* browser,
                    content::WebContents* web_contents)
      : browser_(browser),
        flow_(std::make_unique<image_editor::ScreenshotFlow>(web_contents)) {
    active_ = true;
    flow_->Start(base::BindOnce(&GrokRegionCapture::OnCaptured,
                                base::Unretained(this)));
  }
  GrokRegionCapture(const GrokRegionCapture&) = delete;
  GrokRegionCapture& operator=(const GrokRegionCapture&) = delete;

 private:
  void OnCaptured(const image_editor::ScreenshotCaptureResult& result) {
    active_ = false;
    if (result.result_code ==
            image_editor::ScreenshotCaptureResultCode::SUCCESS &&
        !result.image.IsEmpty()) {
      scoped_refptr<base::RefCountedMemory> png = result.image.As1xPNGBytes();
      if (png && png->size()) {
        base::FilePath f =
            xplorer_paths::DataDir().AppendASCII("pending_image.b64");
        base::CreateDirectory(f.DirName());
        base::WriteFile(f, base::Base64Encode(*png));
      }
      OpenGrokSidePanelAt(browser_, "/?imagesearch=1");
    }
    delete this;
  }
  raw_ptr<BrowserWindowInterface> browser_;
  std::unique_ptr<image_editor::ScreenshotFlow> flow_;
};

// static
bool GrokRegionCapture::active_ = false;
}  // namespace

void GrokImageSearchForTab(BrowserWindowInterface* browser) {
  // XPLORER: replaces Google Lens "Search this tab with Image Search" + the
  // right-click "Search image with Google Lens". Reuse Chromium's ScreenshotFlow
  // so the user drags a region on the live page; the selected region is sent to
  // Grok vision (grok-composer) in the side panel. If the tab can't be resolved,
  // fall back to a whole-tab capture in the sidebar.
  if (!browser)
    return;
  if (GrokRegionCapture::active_)
    return;  // a drag-select overlay is already up; ignore the double-trigger
  tabs::TabInterface* tab = browser->GetActiveTabInterface();
  content::WebContents* wc = tab ? tab->GetContents() : nullptr;
  if (!wc) {
    OpenGrokSidePanelAt(browser, "/?imagesearch=1");
    return;
  }
  new GrokRegionCapture(browser, wc);  // self-owned; deletes itself when done
}

}  // namespace grok_companion