// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/browser_api.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
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
  model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
  result.Set("ok", true);
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