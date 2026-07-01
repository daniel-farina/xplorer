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

inline constexpr char kProductName[] = "Xplor";

inline constexpr char kCompanionHost[] = "127.0.0.1";
// Grok UI is served natively by AgentGateway (default 9334).
inline constexpr int kCompanionPort = 9334;
inline constexpr char kCompanionPath[] = "/";
inline constexpr char kSearchPath[] = "/search";
inline constexpr char kWelcomePath[] = "/welcome";
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

// Reads grok_settings.json and returns the ordered top-level "bookmarks" array
// as a vector of Dicts (one per bookmark: {id,label,url}). Returns an EMPTY
// vector when the file is missing/unparseable or "bookmarks" is absent/non-list
// — the seeder treats empty as "first run" and persists the built-in defaults.
// The synthetic "id" string is parsed to an int64 and stamped on the open tab
// as TabOwnership::bookmark_node_id so the native "Bookmarks" group stays stable
// across launches and config edits.
std::vector<base::DictValue> GetBookmarkConfigs();

// Replaces the ordered top-level "bookmarks" array in grok_settings.json with
// |bookmarks| (each a {id,label,url} dict), saves, and notifies live tab
// groupers. Merge-safe: only the "bookmarks" key is rewritten.
void SetBookmarkConfigs(const std::vector<base::DictValue>& bookmarks);

// Removes the bookmark whose synthetic "id" parses to |node_id| from the
// persisted "bookmarks" array (a no-op if absent), then re-persists via
// SetBookmarkConfigs (which fires the live-reload notify). Used when the user
// manually closes a bookmark tab so the close sticks and a later
// ApplyBookmarkConfig / new window doesn't re-open it.
void RemoveBookmarkConfig(int64_t node_id);

// Live-reload seam. Fired on the UI thread whenever the bookmark list is
// persisted in-process (the gateway's /api/settings write, or
// SetBookmarkConfigs), so open AgentTabGroupers can re-open/close bookmark tabs.
base::CallbackListSubscription AddBookmarkConfigChangedCallback(
    base::RepeatingClosure callback);
// Notifies subscribers. Must be called on the UI thread.
void NotifyBookmarkConfigChanged();

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

// XPLORER: replaces Chrome's Google Lens "Search this tab with Image Search" —
// opens the Grok side panel at the image-search entry (?imagesearch=1), which
// screenshots the active tab and runs a Grok vision analysis.
void GrokImageSearchForTab(BrowserWindowInterface* browser);

// Register the Grok side panel entry on the global side panel registry.
void RegisterGrokSidePanel(BrowserWindowInterface* browser);

}  // namespace grok_companion

#endif  // CHROME_BROWSER_GROK_COMPANION_GROK_COMPANION_UTIL_H_