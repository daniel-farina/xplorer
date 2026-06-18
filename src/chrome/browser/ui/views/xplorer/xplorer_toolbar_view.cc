// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_view.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_icons.h"
#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_pill_button.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/menu_separator_types.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace xplorer {

namespace {

// Built-in default pills, used when grok_settings.json has no toolbar.pills.
// Transcribed (id, label, href, icon, is_home) IN ORDER from
// companion/ui/toolbar.js DEFAULT_PILLS — keep the two in sync.
constexpr struct {
  const char* id;
  const char* label;
  const char* href;
  const char* icon;
  bool is_home;
} kDefaultPills[] = {
    {"xchat", "X Chat", "https://x.com/i/chat", "chat", false},
    {"build", "Grok Build", "/switch-home?mode=build", "wrench", true},
    {"web", "Grok Web", "/switch-home?mode=web", "globe", true},
    {"imagine", "Imagine", "https://grok.com/imagine", "image", false},
    {"wiki", "Groki", "/switch-home?mode=wiki", "book", true},
    {"xcom", "x.com", "https://x.com/", "xmark", false},
    {"xgrok", "Grok on X", "https://x.com/i/grok", "sparkle", false},
};

// Layout constants for the strip. Pills keep their intrinsic height; the strip
// is a touch shorter than the scaffold and centers them vertically.
constexpr int kToolbarHeight = 40;
constexpr int kLeadingMargin = 8;
constexpr int kButtonSpacing = 6;
// Tight gap between a pill and its trailing chevron (reads as one control).
constexpr int kChevronSpacing = 1;

// Menu command-id ranges. Disjoint so the right-click context menu and the
// child dropdown (separate SimpleMenuModels) never collide.
constexpr int kCmdCustomize = 1;
constexpr int kCmdHideToolbar = 2;
constexpr int kCmdEditPillBase = 1000;    // + pill index
constexpr int kCmdRemovePillBase = 2000;  // + pill index
constexpr int kChildCmdBase = 3000;       // + child index

// Action sentinel prefix that switches the profile-scoped search home mode.
constexpr char kSwitchHomePrefix[] = "/switch-home";

// Extracts the value of the "mode" query parameter from a "/switch-home?mode=X"
// href. Returns an empty string if absent.
std::string ExtractMode(const std::string& href) {
  static constexpr std::string_view kModeKey = "mode=";
  const size_t q = href.find('?');
  if (q == std::string::npos) {
    return std::string();
  }
  std::string_view query(href);
  query.remove_prefix(q + 1);
  while (!query.empty()) {
    size_t amp = query.find('&');
    std::string_view pair =
        amp == std::string_view::npos ? query : query.substr(0, amp);
    if (base::StartsWith(pair, kModeKey)) {
      pair.remove_prefix(kModeKey.size());
      return std::string(pair);
    }
    if (amp == std::string_view::npos) {
      break;
    }
    query.remove_prefix(amp + 1);
  }
  return std::string();
}

}  // namespace

XplorerToolbarView::XplorerToolbarView(BrowserWindowInterface* browser,
                                       Profile* profile)
    : browser_(browser), profile_(profile) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kToolbar);
  GetViewAccessibility().SetName(std::string("Xplorer toolbar"));

  // Themed solid background that follows the toolbar color.
  SetBackground(views::CreateSolidBackground(kColorToolbar));

  // Right-click anywhere on the bar background opens the customization menu.
  set_context_menu_controller(this);

  LoadPills();
  RebuildButtons();
}

XplorerToolbarView::~XplorerToolbarView() = default;

void XplorerToolbarView::Reload() {
  LoadPills();
  RebuildButtons();
}

void XplorerToolbarView::LoadPills() {
  pills_.clear();

  // Config-driven: read the ordered toolbar.pills array from grok_settings.json.
  // Each entry: {id,label,icon,href,enabled,isHome}. Skip enabled==false.
  for (const base::DictValue& config :
       grok_companion::GetToolbarPillConfigs()) {
    const std::optional<bool> enabled = config.FindBool("enabled");
    if (enabled.has_value() && !enabled.value()) {
      continue;
    }
    const std::string* id = config.FindString("id");
    if (!id || id->empty()) {
      continue;
    }
    ToolbarPill pill;
    pill.id = *id;
    if (const std::string* label = config.FindString("label")) {
      pill.label = *label;
    }
    if (const std::string* href = config.FindString("href")) {
      pill.href = *href;
    }
    if (const std::string* icon = config.FindString("icon")) {
      pill.icon = *icon;
    }
    pill.is_home = config.FindBool("isHome").value_or(false);
    // Optional dropdown entries: [{label, href}, ...].
    if (const base::ListValue* kids = config.FindList("children")) {
      for (const base::Value& kid : *kids) {
        if (!kid.is_dict()) {
          continue;
        }
        const std::string* l = kid.GetDict().FindString("label");
        const std::string* h = kid.GetDict().FindString("href");
        if (l && h) {
          pill.children.push_back({*l, *h});
        }
      }
    }
    pills_.push_back(std::move(pill));
  }

  // No config (or all entries filtered out): fall back to the built-in defaults.
  if (pills_.empty()) {
    for (const auto& def : kDefaultPills) {
      ToolbarPill pill{def.id, def.label, def.href, def.icon, def.is_home};
      // Default children mirror companion/ui/toolbar.js DEFAULT_PILLS.
      if (pill.id == "build") {
        pill.children = {{"Conversations", "/"},
                         {"Apps", "/apps"},
                         {"Logs", "/logs"}};
      } else if (pill.id == "web") {
        pill.children = {{"Search", "/search"}};
      }
      pills_.push_back(std::move(pill));
    }
  }
}

void XplorerToolbarView::RebuildButtons() {
  pill_buttons_.clear();
  RemoveAllChildViews();

  // The active "home" pill is the one whose mode matches the current search
  // home mode; non-home pills are never selected.
  const std::string active_mode = grok_companion::GetSearchHomeMode();

  for (size_t i = 0; i < pills_.size(); ++i) {
    PillViews views;

    auto button = std::make_unique<XplorerToolbarPillButton>(
        base::BindRepeating(&XplorerToolbarView::OnPillPressed,
                            base::Unretained(this), i),
        base::UTF8ToUTF16(pills_[i].label));
    button->SetPillIcon(GetToolbarVectorIcon(pills_[i].icon));
    button->GetViewAccessibility().SetName(base::UTF8ToUTF16(pills_[i].label));
    if (pills_[i].is_home && !active_mode.empty() &&
        ExtractMode(pills_[i].href) == active_mode) {
      button->SetSelected(true);
    }
    // Right-click a pill -> context menu scoped to that pill (Edit/Remove).
    button->set_context_menu_controller(this);
    views.main = AddChildView(std::move(button));

    // Pills with children get a trailing chevron pill that opens the dropdown.
    // Primary click on the main pill still navigates the pill's own href.
    if (!pills_[i].children.empty()) {
      auto chevron = std::make_unique<XplorerToolbarPillButton>(
          base::BindRepeating(&XplorerToolbarView::ShowChildMenu,
                              base::Unretained(this), i));
      chevron->SetPillIcon(GetToolbarVectorIcon("caret"));
      chevron->GetViewAccessibility().SetName(
          base::UTF8ToUTF16("More " + pills_[i].label + " options"));
      chevron->set_context_menu_controller(this);
      views.chevron = AddChildView(std::move(chevron));
    }

    pill_buttons_.push_back(views);
  }
}

gfx::Size XplorerToolbarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // Full available width; fixed scaffold height.
  int width = 0;
  if (available_size.width().is_bounded()) {
    width = available_size.width().value();
  }
  return gfx::Size(width, kToolbarHeight);
}

void XplorerToolbarView::Layout(PassKey) {
  const int content_height = GetContentsBounds().height();
  int x = kLeadingMargin;
  auto place = [&](XplorerToolbarPillButton* button, int trailing_gap) {
    const gfx::Size preferred = button->GetPreferredSize();
    // Let the pill keep its intrinsic ~28px height; vertically center it.
    const int button_height = std::min(preferred.height(), content_height);
    const int y = (content_height - button_height) / 2;
    button->SetBounds(x, y, preferred.width(), button_height);
    x += preferred.width() + trailing_gap;
  };
  for (const PillViews& views : pill_buttons_) {
    place(views.main, views.chevron ? kChevronSpacing : kButtonSpacing);
    if (views.chevron) {
      place(views.chevron, kButtonSpacing);
    }
  }
}

GURL XplorerToolbarView::ResolveHref(const std::string& href) const {
  // Gateway-relative path (e.g. "/search"): resolve against the companion base.
  if (!href.empty() && href.front() == '/') {
    return grok_companion::GetCompanionURL().Resolve(href);
  }
  return GURL(href);
}

void XplorerToolbarView::OnPillPressed(size_t pill_index) {
  if (pill_index >= pills_.size()) {
    return;
  }
  const ToolbarPill& pill = pills_[pill_index];

  // Action sentinel: switch the profile-scoped search home mode, then open the
  // (now-updated) Grok search home in the active tab.
  if (base::StartsWith(pill.href, kSwitchHomePrefix)) {
    const std::string mode = ExtractMode(pill.href);
    if (!mode.empty()) {
      grok_companion::SetSearchHomeMode(mode);
    }
    grok_companion::OpenGrokSearchPage(browser_);
    return;
  }

  // Otherwise navigate the active tab to the pill's resolved URL.
  Navigate(ResolveHref(pill.href));
}

void XplorerToolbarView::Navigate(const GURL& url) {
  // Same navigation pattern as grok_companion::OpenGrokSearchPage.
  if (!url.is_valid() || !browser_) {
    return;
  }
  tabs::TabInterface* tab = browser_->GetActiveTabInterface();
  if (!tab) {
    return;
  }
  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    return;
  }
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  contents->GetController().LoadURLWithParams(params);
}

void XplorerToolbarView::ShowChildMenu(size_t pill_index) {
  if (pill_index >= pills_.size() || pills_[pill_index].children.empty()) {
    return;
  }
  open_pill_ = pill_index;

  child_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  const std::vector<ToolbarChild>& children = pills_[pill_index].children;
  for (size_t c = 0; c < children.size(); ++c) {
    child_menu_model_->AddItem(kChildCmdBase + static_cast<int>(c),
                               base::UTF8ToUTF16(children[c].label));
  }

  // Anchor under the chevron (fall back to the main pill).
  views::View* anchor = pill_buttons_[pill_index].chevron
                            ? static_cast<views::View*>(
                                  pill_buttons_[pill_index].chevron.get())
                            : static_cast<views::View*>(
                                  pill_buttons_[pill_index].main.get());
  if (!anchor || !GetWidget()) {
    return;
  }
  child_menu_runner_ = std::make_unique<views::MenuRunner>(
      child_menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  child_menu_runner_->RunMenuAt(
      GetWidget(), /*button_controller=*/nullptr, anchor->GetBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kMouse);
}

void XplorerToolbarView::ExecuteCommand(int command_id, int event_flags) {
  // Child dropdown items (highest range first).
  if (command_id >= kChildCmdBase) {
    const size_t child = static_cast<size_t>(command_id - kChildCmdBase);
    if (open_pill_ < pills_.size() &&
        child < pills_[open_pill_].children.size()) {
      Navigate(ResolveHref(pills_[open_pill_].children[child].href));
    }
    return;
  }
  // Remove pill (range [2000,3000)) — first cut opens the /settings editor;
  // true inline remove is a follow-up (needs Load/SaveGrokSettings promoted out
  // of grok_companion_util.cc's anonymous namespace).
  if (command_id >= kCmdRemovePillBase) {
    OpenCustomizePage();
    return;
  }
  // Edit pill (range [1000,2000)) — likewise opens the editor for now.
  if (command_id >= kCmdEditPillBase) {
    OpenCustomizePage();
    return;
  }
  // Bar-level actions.
  switch (command_id) {
    case kCmdCustomize:
      OpenCustomizePage();
      break;
    case kCmdHideToolbar:
      // Immediate hide for this window; persistence across restart is a
      // follow-up (a toolbar.hidden flag read by the layout gate).
      SetVisible(false);
      break;
    default:
      break;
  }
}

bool XplorerToolbarView::IsCommandIdChecked(int command_id) const {
  return false;
}

bool XplorerToolbarView::IsCommandIdEnabled(int command_id) const {
  return true;
}

int XplorerToolbarView::PillIndexForView(const views::View* view) const {
  for (size_t i = 0; i < pill_buttons_.size(); ++i) {
    if (pill_buttons_[i].main == view || pill_buttons_[i].chevron == view) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void XplorerToolbarView::OpenCustomizePage() {
  Navigate(grok_companion::GetCompanionURL().Resolve("/settings"));
}

void XplorerToolbarView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  context_pill_ = PillIndexForView(source);

  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  context_menu_model_->AddItem(
      kCmdCustomize, base::UTF8ToUTF16(std::string("Customize toolbar...")));
  context_menu_model_->AddItem(
      kCmdHideToolbar, base::UTF8ToUTF16(std::string("Hide toolbar")));
  if (context_pill_ >= 0 &&
      static_cast<size_t>(context_pill_) < pills_.size()) {
    context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    context_menu_model_->AddItem(
        kCmdEditPillBase + context_pill_,
        base::UTF8ToUTF16("Edit \"" + pills_[context_pill_].label + "\"..."));
    context_menu_model_->AddItem(
        kCmdRemovePillBase + context_pill_,
        base::UTF8ToUTF16(std::string("Remove pill")));
  }

  if (!GetWidget()) {
    return;
  }
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), /*button_controller=*/nullptr,
      gfx::Rect(point, gfx::Size()), views::MenuAnchorPosition::kTopLeft,
      source_type);
}

BEGIN_METADATA(XplorerToolbarView)
END_METADATA

}  // namespace xplorer
