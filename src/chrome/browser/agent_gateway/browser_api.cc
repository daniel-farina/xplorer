// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/browser_api.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/agent_gateway/tab_ownership.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace agent_gateway {
namespace {

TabStripModel* FindTabStrip(const std::string& tab_id, int* out_index) {
  std::vector<std::string> parts;
  size_t colon = tab_id.find(':');
  if (colon == std::string::npos)
    return nullptr;
  int sid = 0, index = 0;
  if (!base::StringToInt(tab_id.substr(0, colon), &sid) ||
      !base::StringToInt(tab_id.substr(colon + 1), &index)) {
    return nullptr;
  }
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser->GetSessionID().id() != sid)
      continue;
    TabStripModel* model = browser->GetTabStripModel();
    if (index >= 0 && index < model->count()) {
      *out_index = index;
      return model;
    }
  }
  return nullptr;
}

void AppendBookmarkNode(const bookmarks::BookmarkNode* node,
                        base::ListValue& out,
                        int depth) {
  if (!node)
    return;
  if (node->is_url()) {
    base::DictValue b;
    b.Set("id", base::NumberToString(node->id()));
    b.Set("type", "url");
    b.Set("title", base::UTF16ToUTF8(node->GetTitle()));
    b.Set("url", node->url().spec());
    b.Set("parent_id", base::NumberToString(node->parent()->id()));
    b.Set("depth", depth);
    out.Append(std::move(b));
  } else {
    base::DictValue f;
    f.Set("id", base::NumberToString(node->id()));
    f.Set("type", "folder");
    f.Set("title", base::UTF16ToUTF8(node->GetTitle()));
    f.Set("parent_id",
          node->parent() ? base::NumberToString(node->parent()->id()) : "0");
    f.Set("depth", depth);
    out.Append(std::move(f));
    for (const auto& child : node->children())
      AppendBookmarkNode(child.get(), out, depth + 1);
  }
}

const bookmarks::BookmarkNode* FindBookmarkNode(
    bookmarks::BookmarkModel* model,
    const std::string& id_str) {
  int64_t id = 0;
  if (!base::StringToInt64(id_str, &id))
    return nullptr;
  return bookmarks::GetBookmarkNodeByID(model, id);
}

}  // namespace

void BrowserApi::ListBookmarks(DictCallback callback) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  bookmarks::BookmarkModel* model =
      BookmarkModelFactory::GetForBrowserContext(profile);
  base::DictValue result;
  base::ListValue items;
  if (!model || !model->loaded()) {
    result.Set("error", "bookmark model not loaded");
    std::move(callback).Run(std::move(result));
    return;
  }
  AppendBookmarkNode(model->bookmark_bar_node(), items, 0);
  AppendBookmarkNode(model->other_node(), items, 0);
  result.Set("bookmarks", std::move(items));
  std::move(callback).Run(std::move(result));
}

void BrowserApi::AddBookmark(const std::string& url,
                             const std::string& title,
                             const std::string& parent_id,
                             DictCallback callback) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  bookmarks::BookmarkModel* model =
      BookmarkModelFactory::GetForBrowserContext(profile);
  base::DictValue result;
  if (!model || !model->loaded()) {
    result.Set("error", "bookmark model not loaded");
    std::move(callback).Run(std::move(result));
    return;
  }
  const bookmarks::BookmarkNode* parent = model->bookmark_bar_node();
  if (!parent_id.empty()) {
    parent = FindBookmarkNode(model, parent_id);
    if (!parent || !parent->is_folder()) {
      result.Set("error", "parent folder not found");
      std::move(callback).Run(std::move(result));
      return;
    }
  }
  GURL gurl(url);
  if (!gurl.is_valid()) {
    result.Set("error", "invalid url");
    std::move(callback).Run(std::move(result));
    return;
  }
  std::u16string u_title = base::UTF8ToUTF16(title);
  if (u_title.empty())
    u_title = base::UTF8ToUTF16(gurl.host());
  const bookmarks::BookmarkNode* node =
      model->AddURL(parent, parent->children().size(), u_title, gurl);
  result.Set("ok", true);
  result.Set("id", base::NumberToString(node->id()));
  std::move(callback).Run(std::move(result));
}

void BrowserApi::RemoveBookmark(const std::string& id,
                                DictCallback callback) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  bookmarks::BookmarkModel* model =
      BookmarkModelFactory::GetForBrowserContext(profile);
  base::DictValue result;
  const bookmarks::BookmarkNode* node = FindBookmarkNode(model, id);
  if (!node) {
    result.Set("error", "bookmark not found");
    std::move(callback).Run(std::move(result));
    return;
  }
  model->Remove(node, bookmarks::metrics::BookmarkEditSource::kOther,
                FROM_HERE);
  result.Set("ok", true);
  std::move(callback).Run(std::move(result));
}

void BrowserApi::QueryHistory(const std::string& query,
                              int limit,
                              DictCallback callback) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  history::HistoryService* history =
      HistoryServiceFactory::GetForProfile(profile,
                                           ServiceAccessType::EXPLICIT_ACCESS);
  base::DictValue result;
  if (!history) {
    result.Set("error", "history service unavailable");
    std::move(callback).Run(std::move(result));
    return;
  }
  if (limit <= 0)
    limit = 50;
  auto* tracker = new base::CancelableTaskTracker();
  history->QueryHistory(
      base::UTF8ToUTF16(query), history::QueryOptions(),
      base::BindOnce(
          [](DictCallback cb, int lim, base::CancelableTaskTracker* trk,
             history::QueryResults results) {
            delete trk;
            base::DictValue out;
            base::ListValue entries;
            int count = 0;
            for (const auto& row : results) {
              if (count++ >= lim)
                break;
              base::DictValue e;
              e.Set("url", row.url().spec());
              e.Set("title", base::UTF16ToUTF8(row.title()));
              e.Set("visit_count", static_cast<int>(row.visit_count()));
              e.Set("typed_count", static_cast<int>(row.typed_count()));
              e.Set("last_visit",
                    static_cast<double>(
                        row.last_visit().InSecondsFSinceUnixEpoch()));
              entries.Append(std::move(e));
            }
            out.Set("history", std::move(entries));
            std::move(cb).Run(std::move(out));
          },
          std::move(callback), limit, tracker),
      tracker);
}

void BrowserApi::ActivateTab(const std::string& tab_id,
                             DictCallback callback) {
  base::DictValue result;
  int index = 0;
  TabStripModel* model = FindTabStrip(tab_id, &index);
  if (!model) {
    result.Set("error", "tab not found");
    std::move(callback).Run(std::move(result));
    return;
  }
  model->ActivateTabAt(index);
  result.Set("ok", true);
  std::move(callback).Run(std::move(result));
}

void BrowserApi::CloseTab(const std::string& tab_id, DictCallback callback) {
  base::DictValue result;
  int index = 0;
  TabStripModel* model = FindTabStrip(tab_id, &index);
  if (!model) {
    result.Set("error", "tab not found");
    std::move(callback).Run(std::move(result));
    return;
  }
  // A persistent bookmark tab (the xAI "Bookmarks" group) must NOT be closeable
  // via the API: an agent or scheduled task could otherwise close it, which drops
  // it from the bookmark config -- the recurring bookmark-config drift was a
  // scheduled-task agent DELETE'ing bookmark tabs over idle gaps. The user can
  // still close one via the native tab X (a different, non-API path), which is the
  // intended closeable behavior; agents manage bookmarks via /api/settings.
  content::WebContents* wc = model->GetWebContentsAt(index);
  TabOwnership* own = wc ? TabOwnership::Get(wc) : nullptr;
  if (own && own->bookmark_node_id != 0) {
    result.Set("error",
               "cannot close a bookmark tab via the API; manage bookmarks via "
               "/api/settings");
    std::move(callback).Run(std::move(result));
    return;
  }
  model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
  result.Set("ok", true);
  std::move(callback).Run(std::move(result));
}

std::string TabCategory(const GURL& url, const std::string& title) {
  const std::string spec = base::ToLowerASCII(url.spec());
  const std::string host = base::ToLowerASCII(url.host());
  const std::string lower_title = base::ToLowerASCII(title);
  if (host == "grok.com" || lower_title.find("grok") != std::string::npos)
    return "Grok";
  if (host == "grokipedia.com")
    return "Wiki";
  if (host == "127.0.0.1" && (url.port() == "9334" || spec.find(":9334") != std::string::npos))
    return "Xplorer";
  if (host.find("news") != std::string::npos ||
      host.find("cnn.com") != std::string::npos ||
      host.find("bbc.") != std::string::npos ||
      lower_title.find("news") != std::string::npos)
    return "News";
  if (spec.find("/travel/flights") != std::string::npos ||
      lower_title.find("flight") != std::string::npos)
    return "Travel";
  if (host == "localhost" ||
      (host == "127.0.0.1" && url.port() != "9334"))
    return "Development";
  if (url.SchemeIs("chrome"))
    return "Browser";
  return "Misc";
}

tab_groups::TabGroupColorId ColorForCategory(const std::string& category) {
  if (category == "Grok")
    return tab_groups::TabGroupColorId::kPurple;
  if (category == "Xplorer")
    return tab_groups::TabGroupColorId::kBlue;
  if (category == "News")
    return tab_groups::TabGroupColorId::kRed;
  if (category == "Travel")
    return tab_groups::TabGroupColorId::kGreen;
  if (category == "Development")
    return tab_groups::TabGroupColorId::kYellow;
  if (category == "Wiki")
    return tab_groups::TabGroupColorId::kCyan;
  if (category == "Browser")
    return tab_groups::TabGroupColorId::kGrey;
  return tab_groups::TabGroupColorId::kOrange;
}

namespace {
// TabStripModel::AddToNewGroup() CHECK-aborts the entire browser process on
// duplicate or out-of-range indices. A model driving the browser can easily pass
// repeated, reordered, or stale tab ids (especially after opening many tabs), so
// always dedupe, drop anything no longer in the strip, and sort ascending before
// grouping — a bad tool argument must degrade gracefully, never crash.
std::vector<int> SanitizeTabIndices(TabStripModel* model,
                                    std::vector<int> indices) {
  std::vector<int> clean;
  for (int i : indices) {
    if (model->ContainsIndex(i) &&
        std::find(clean.begin(), clean.end(), i) == clean.end()) {
      clean.push_back(i);
    }
  }
  std::sort(clean.begin(), clean.end());
  return clean;
}
}  // namespace

void BrowserApi::OrganizeTabs(DictCallback callback) {
  base::DictValue result;
  base::ListValue groups_out;
  int total_tabs = 0;
  int groups_created = 0;

  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    TabStripModel* model = browser->GetTabStripModel();
    std::map<std::string, std::vector<int>> buckets;
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* wc = model->GetWebContentsAt(i);
      if (!wc)
        continue;
      // Skip managed tabs (bookmarks / agent-owned / scheduled-task). They belong
      // to AgentTabGrouper's persistent Bookmarks/Agent/Scheduled groups; letting
      // organize bucket them by category scatters them out of those groups and the
      // grouper does not reclaim a tab already in a non-managed group.
      TabOwnership* own = TabOwnership::Get(wc);
      if (own && (own->bookmark_node_id != 0 || !own->owner.empty() ||
                  !own->task_id.empty())) {
        continue;
      }
      ++total_tabs;
      const std::string category = TabCategory(
          wc->GetLastCommittedURL(),
          base::UTF16ToUTF8(wc->GetTitle()));
      buckets[category].push_back(i);
    }
    for (auto& [category, indices] : buckets) {
      indices = SanitizeTabIndices(model, std::move(indices));
      if (indices.empty())
        continue;
      tab_groups::TabGroupId group = model->AddToNewGroup(indices);
      tab_groups::TabGroupVisualData visual_data(
          base::UTF8ToUTF16(category), ColorForCategory(category));
      model->ChangeTabGroupVisuals(group, visual_data);
      ++groups_created;

      base::DictValue g;
      g.Set("title", category);
      g.Set("group_id", group.ToString());
      base::ListValue tab_ids;
      const int sid = browser->GetSessionID().id();
      for (int idx : indices) {
        tab_ids.Append(base::NumberToString(sid) + ":" +
                       base::NumberToString(idx));
      }
      g.Set("tab_ids", std::move(tab_ids));
      groups_out.Append(std::move(g));
    }
  }

  result.Set("ok", true);
  result.Set("tabs", total_tabs);
  result.Set("groups_created", groups_created);
  result.Set("groups", std::move(groups_out));
  std::move(callback).Run(std::move(result));
}

void BrowserApi::GroupTabs(const std::vector<std::string>& tab_ids,
                            const std::string& title,
                            DictCallback callback) {
  base::DictValue result;
  if (tab_ids.empty()) {
    result.Set("error", "no tabs specified");
    std::move(callback).Run(std::move(result));
    return;
  }
  TabStripModel* model = nullptr;
  std::vector<int> indices;
  for (const std::string& id : tab_ids) {
    int index = 0;
    TabStripModel* m = FindTabStrip(id, &index);
    if (!m) {
      result.Set("error", "tab not found: " + id);
      std::move(callback).Run(std::move(result));
      return;
    }
    if (!model)
      model = m;
    else if (model != m) {
      result.Set("error", "tabs must be in the same window");
      std::move(callback).Run(std::move(result));
      return;
    }
    indices.push_back(index);
  }
  indices = SanitizeTabIndices(model, std::move(indices));
  if (indices.empty()) {
    result.Set("error", "no valid tabs to group");
    std::move(callback).Run(std::move(result));
    return;
  }
  tab_groups::TabGroupId group = model->AddToNewGroup(indices);
  if (!title.empty()) {
    tab_groups::TabGroupVisualData visual_data(base::UTF8ToUTF16(title),
                                               tab_groups::TabGroupColorId::kGrey);
    model->ChangeTabGroupVisuals(group, visual_data);
  }
  result.Set("ok", true);
  result.Set("group_id", group.ToString());
  std::move(callback).Run(std::move(result));
}

void BrowserApi::SplitTab(const std::string& tab_id,
                          const std::string& layout,
                          DictCallback callback) {
  base::DictValue result;
  int index = 0;
  TabStripModel* model = FindTabStrip(tab_id, &index);
  if (!model) {
    result.Set("error", "tab not found");
    std::move(callback).Run(std::move(result));
    return;
  }
  model->ActivateTabAt(index);
  BrowserWindowInterface* browser = nullptr;
  for (BrowserWindowInterface* b : GetAllBrowserWindowInterfaces()) {
    if (b->GetTabStripModel() == model) {
      browser = b;
      break;
    }
  }
  if (!browser) {
    result.Set("error", "browser window not found");
    std::move(callback).Run(std::move(result));
    return;
  }
  split_tabs::SplitTabLayout split_layout =
      layout == "stacked" ? split_tabs::SplitTabLayout::kStacked
                          : split_tabs::SplitTabLayout::kSideBySide;
  chrome::NewSplitTab(browser, split_layout,
                      split_tabs::SplitTabCreatedSource::kExtensionsApi);
  result.Set("ok", true);
  std::move(callback).Run(std::move(result));
}

void BrowserApi::GetTheme(DictCallback callback) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ThemeService* theme = ThemeServiceFactory::GetForProfile(profile);
  base::DictValue result;
  if (!theme) {
    result.Set("error", "theme service unavailable");
    std::move(callback).Run(std::move(result));
    return;
  }
  switch (theme->GetBrowserColorScheme()) {
    case ThemeService::BrowserColorScheme::kDark:
      result.Set("color_scheme", "dark");
      break;
    case ThemeService::BrowserColorScheme::kLight:
      result.Set("color_scheme", "light");
      break;
    case ThemeService::BrowserColorScheme::kSystem:
    default:
      result.Set("color_scheme", "system");
      break;
  }
  result.Set("using_custom_theme", theme->UsingExtensionTheme());
  std::move(callback).Run(std::move(result));
}

void BrowserApi::SetTheme(const std::string& color_scheme,
                          DictCallback callback) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ThemeService* theme = ThemeServiceFactory::GetForProfile(profile);
  base::DictValue result;
  if (!theme) {
    result.Set("error", "theme service unavailable");
    std::move(callback).Run(std::move(result));
    return;
  }
  ThemeService::BrowserColorScheme scheme =
      ThemeService::BrowserColorScheme::kSystem;
  if (color_scheme == "dark")
    scheme = ThemeService::BrowserColorScheme::kDark;
  else if (color_scheme == "light")
    scheme = ThemeService::BrowserColorScheme::kLight;
  theme->SetBrowserColorScheme(scheme);
  result.Set("ok", true);
  result.Set("color_scheme", color_scheme);
  std::move(callback).Run(std::move(result));
}

}  // namespace agent_gateway