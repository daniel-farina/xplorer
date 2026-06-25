// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_GROK_COMPANION_GROK_COMPANION_UTIL_H_
#define CHROME_BROWSER_GROK_COMPANION_GROK_COMPANION_UTIL_H_

#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "url/gurl.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}  // namespace content

namespace grok_companion {

inline constexpr char kProductName[] = "Xplorer";
inline constexpr char kXplorerDataDir[] = ".xplorer";

inline constexpr char kCompanionHost[] = "127.0.0.1";
// Grok UI is served natively by AgentGateway (default 9334).
inline constexpr int kCompanionPort = 9334;
inline constexpr char kCompanionPath[] = "/";
inline constexpr char kSearchPath[] = "/search";
inline constexpr char kWelcomePath[] = "/welcome";
inline constexpr char kGrokWebHomeURL[] = "https://grok.com/";
inline constexpr char kGrokWikiHomeURL[] = "https://grokipedia.com/";
inline constexpr char kSearchHomeBuild[] = "build";
inline constexpr char kSearchHomeWeb[] = "web";
inline constexpr char kSearchHomeWiki[] = "wiki";

// Sites that receive the injected Grok top toolbar (not the page FAB).
inline bool IsGrokToolbarOverlayHost(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS())
    return false;
  const std::string_view host = url.host();
  if (host == "grok.com" || host == "www.grok.com" ||
      base::EndsWith(host, ".grok.com")) {
    return true;
  }
  if (host == "grokipedia.com" || host == "www.grokipedia.com" ||
      base::EndsWith(host, ".grokipedia.com")) {
    return true;
  }
  if (host == "x.com" || host == "www.x.com" ||
      base::EndsWith(host, ".x.com")) {
    return true;
  }
  if (host == "twitter.com" || host == "www.twitter.com" ||
      base::EndsWith(host, ".twitter.com")) {
    return true;
  }
  return false;
}

base::FilePath GetXplorerDataDir();
base::FilePath ResolveDataFile(const char* filename);

GURL GetCompanionURL();
GURL GetSearchURL();
GURL GetWelcomeURL();
GURL GetStartupHomeURL();

// True if `url` is one of Xplorer's own gateway home pages (the new-tab home:
// search / companion / welcome). Such pages render with a blank omnibox, like
// the NTP, instead of exposing the internal gateway URL.
bool IsGrokHomeURL(const GURL& url);

bool HasCompletedWelcome();
void MarkWelcomeCompleted();

// User preference: native Grok Build UI vs grok.com (stored in grok_settings.json).
std::string GetSearchHomeMode();
void SetSearchHomeMode(const std::string& mode);

// Reads grok_settings.json and returns the ordered "toolbar.pills" array as a
// vector of Dicts (one per pill: {id,label,icon,href,enabled,isHome}). Returns
// an EMPTY vector when the file is missing/unparseable or "toolbar.pills" is
// absent/empty/non-list, so callers fall back to their built-in defaults. The
// schema mirrors companion/ui/toolbar.js DEFAULT_PILLS.
std::vector<base::DictValue> GetToolbarPillConfigs();

// Replaces the ordered "toolbar.pills" array in grok_settings.json with |pills|
// (each a {id,label,icon,href,enabled,isHome,children} dict) and notifies live
// toolbar views. Used by drag-to-reorder / inline edit.
void SetToolbarPillConfigs(const std::vector<base::DictValue>& pills);

// Live-reload seam. Fired on the UI thread whenever toolbar config is persisted
// in-process (the gateway's toolbar write, or SetToolbarPillConfigs), so open
// XplorerToolbarViews can Reload(). The subscriber holds the returned
// subscription for as long as it wants callbacks (RAII unsubscribe).
base::CallbackListSubscription AddToolbarConfigChangedCallback(
    base::RepeatingClosure callback);
// Notifies subscribers. Must be called on the UI thread.
void NotifyToolbarConfigChanged();

// NTP / omnibox Grok chip destination based on search_home preference.
GURL GetDefaultSearchHomeURL();

// True for chrome://newtab and other internal NTP URLs that block injectors.
bool IsLegacyChromeNewTab(const GURL& url);

// Redirect restored / stale NTP tabs to the real Grok home URL.
void RedirectLegacyNewTabIfNeeded(content::WebContents* contents);

// Open Grok Search home (build or web) in the active tab.
void OpenGrokSearchPage(BrowserWindowInterface* browser);

// "Ask Grok about this page": navigates |web_contents| to Grok with a prompt
// about the current page. Rewires Chrome's "Ask Google about this page" action.
void AskGrokAboutPage(content::WebContents* web_contents);

// Toggle the Grok side panel (chat UI). Called from the toolbar Grok button.
void ToggleGrokSidePanel(BrowserWindowInterface* browser);

// Ensure the Grok side panel (chat UI) is OPEN. Idempotent: shows it if closed,
// no-op if already open; never closes. Used by /api/sidepanel/open (app-create).
void OpenGrokSidePanel(BrowserWindowInterface* browser);

// Open the Grok side panel AND navigate its WebContents to the companion URL +
// |path| (e.g. "/schedules?id=job_ab12"). Opens the panel if closed (idempotent,
// like OpenGrokSidePanel) then loads the path in the panel's contents, so an
// already-open panel jumps to the new path too. |path| is resolved relative to
// GetCompanionURL(); pass a leading-slash absolute path.
void OpenGrokSidePanelAt(BrowserWindowInterface* browser,
                         const std::string& path);

// Register the Grok side panel entry on the global side panel registry.
void RegisterGrokSidePanel(BrowserWindowInterface* browser);

}  // namespace grok_companion

#endif  // CHROME_BROWSER_GROK_COMPANION_GROK_COMPANION_UTIL_H_