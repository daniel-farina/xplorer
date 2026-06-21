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
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/vector2d.h"
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
#include "ui/events/event.h"
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
    {"build", "Grok Build", "/apps", "wrench", false},
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

  // Live-reload when toolbar config is persisted (settings page or gateway).
  // The subscription is a member, so it unsubscribes before |this| is gone;
  // base::Unretained is safe.
  toolbar_config_subscription_ =
      grok_companion::AddToolbarConfigChangedCallback(base::BindRepeating(
          &XplorerToolbarView::Reload, base::Unretained(this)));

  // Track the foreground tab so the home-pill highlight follows the current
  // page rather than the persisted search-home mode. Re-observe on every tab
  // switch; PrimaryPageChanged handles in-tab navigations.
  if (browser_) {
    active_tab_subscription_ = browser_->RegisterActiveTabDidChange(
        base::BindRepeating(&XplorerToolbarView::OnActiveTabChanged,
                            base::Unretained(this)));
    OnActiveTabChanged(browser_);
  }
}

XplorerToolbarView::~XplorerToolbarView() = default;

void XplorerToolbarView::Reload() {
  LoadPills();
  RebuildButtons();
}

void XplorerToolbarView::OnActiveTabChanged(BrowserWindowInterface* browser) {
  content::WebContents* contents = nullptr;
  if (browser) {
    if (tabs::TabInterface* tab = browser->GetActiveTabInterface()) {
      contents = tab->GetContents();
    }
  }
  Observe(contents);  // content::WebContentsObserver
  UpdateActiveHighlight();
}

void XplorerToolbarView::PrimaryPageChanged(content::Page& /*page*/) {
  UpdateActiveHighlight();
}

void XplorerToolbarView::UpdateActiveHighlight() {
  content::WebContents* contents = web_contents();
  const GURL current = contents ? contents->GetLastCommittedURL() : GURL();
  const std::string active_mode = grok_companion::GetSearchHomeMode();
  const GURL home = grok_companion::GetStartupHomeURL();

  // Highlight the one pill that best matches the active tab's page: same host
  // plus the longest matching path prefix wins (so x.com/i/chat lights "X Chat",
  // not "x.com"). Home pills (mode switchers) target the Grok home itself,
  // qualified by the active search-home mode. Pages matching no pill light none.
  int best = -1;
  size_t best_score = 0;
  if (current.is_valid() && !current.host().empty()) {
    for (size_t i = 0; i < pills_.size(); ++i) {
      size_t score = 0;
      bool match = false;
      if (pills_[i].is_home) {
        if (!home.host().empty() && current.host() == home.host() &&
            current.path() == home.path() && !active_mode.empty() &&
            ExtractMode(pills_[i].href) == active_mode) {
          match = true;
          score = home.path().size() + 1;  // edge out a bare host match
        }
      } else {
        const GURL target = ResolveHref(pills_[i].href);
        if (target.is_valid() && !target.host().empty() &&
            current.host() == target.host() &&
            base::StartsWith(current.path(), target.path(),
                             base::CompareCase::SENSITIVE)) {
          match = true;
          score = target.path().size();
        }
      }
      if (match && (best < 0 || score > best_score)) {
        best = static_cast<int>(i);
        best_score = score;
      }
    }
  }

  for (size_t i = 0; i < pills_.size() && i < pill_buttons_.size(); ++i) {
    if (pill_buttons_[i].main) {
      pill_buttons_[i].main->SetSelected(static_cast<int>(i) == best);
    }
  }
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
      // Grok Build keeps its useful submenu; Grok Web's redundant single-item
      // "Search" dropdown is intentionally dropped.
      if (pill.id == "build") {
        // "Conversations" opens the native Grok side panel (same conversations
        // as the agentic sidebar) rather than navigating to a duplicate page.
        pill.children = {{"Conversations", "#sidepanel"},
                         {"Apps", "/apps"},
                         {"Logs", "/logs"}};
      }
      pills_.push_back(std::move(pill));
    }
  }
}

void XplorerToolbarView::RebuildButtons() {
  pill_buttons_.clear();
  RemoveAllChildViews();

  for (size_t i = 0; i < pills_.size(); ++i) {
    PillViews views;

    auto button = std::make_unique<XplorerToolbarPillButton>(
        base::BindRepeating(&XplorerToolbarView::OnPillActivated,
                            base::Unretained(this), i),
        base::UTF8ToUTF16(pills_[i].label));
    button->SetPillIcon(GetToolbarVectorIcon(pills_[i].icon));
    button->GetViewAccessibility().SetName(base::UTF8ToUTF16(pills_[i].label));
    // Pills with children show an integrated trailing caret; a click in the
    // caret zone opens the dropdown, the rest of the pill navigates the href.
    if (!pills_[i].children.empty()) {
      button->SetHasDropdownCaret(true);
    }
    // Right-click a pill -> context menu scoped to that pill (Edit/Remove).
    button->set_context_menu_controller(this);
    // Drag a pill to reorder; the toolbar view is the drag source controller.
    button->set_drag_controller(this);
    views.main = AddChildView(std::move(button));

    pill_buttons_.push_back(views);
  }

  // Selection tracks the active tab's page, not the persisted search-home mode.
  UpdateActiveHighlight();
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
    place(views.main, kButtonSpacing);
  }
}

GURL XplorerToolbarView::ResolveHref(const std::string& href) const {
  // Gateway-relative path (e.g. "/search"): resolve against the companion base.
  if (!href.empty() && href.front() == '/') {
    return grok_companion::GetCompanionURL().Resolve(href);
  }
  return GURL(href);
}

void XplorerToolbarView::OnPillActivated(size_t pill_index,
                                         const ui::Event& event) {
  if (pill_index >= pills_.size() || pill_index >= pill_buttons_.size()) {
    return;
  }
  // A click in the integrated caret zone opens the dropdown (for pills with
  // children); anything else navigates the pill's own href.
  if (!pills_[pill_index].children.empty() && event.IsLocatedEvent()) {
    const gfx::Point location = event.AsLocatedEvent()->location();
    XplorerToolbarPillButton* button = pill_buttons_[pill_index].main;
    if (button && button->PointInCaret(location)) {
      ShowChildMenu(pill_index);
      return;
    }
  }
  OnPillPressed(pill_index);
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

  // Anchor under the pill button (the caret lives inside it).
  views::View* anchor =
      static_cast<views::View*>(pill_buttons_[pill_index].main.get());
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
  // Bar-level actions first (exact match, lowest ids) so they can never be
  // shadowed by a range check.
  if (command_id == kCmdCustomize) {
    OpenCustomizePage();
    return;
  }
  if (command_id == kCmdHideToolbar) {
    // Immediate hide for this window; persistence across restart is a follow-up
    // (a toolbar.hidden flag read by the layout gate).
    SetVisible(false);
    return;
  }
  // Child dropdown items (range [3000, ...)).
  if (command_id >= kChildCmdBase) {
    const size_t child = static_cast<size_t>(command_id - kChildCmdBase);
    if (open_pill_ < pills_.size() &&
        child < pills_[open_pill_].children.size()) {
      const std::string& href = pills_[open_pill_].children[child].href;
      if (href == "#sidepanel") {
        // Open the agentic Grok side panel instead of navigating a tab.
        grok_companion::ToggleGrokSidePanel(browser_);
      } else {
        Navigate(ResolveHref(href));
      }
    }
    return;
  }
  // Edit pill [1000,2000) and Remove pill [2000,3000) — first cut opens the
  // rich /settings editor for both (true inline edit/remove is a follow-up).
  if (command_id >= kCmdEditPillBase) {
    OpenCustomizePage();
    return;
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
    if (pill_buttons_[i].main == view) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void XplorerToolbarView::OpenCustomizePage() {
  Navigate(grok_companion::GetCompanionURL().Resolve("/settings"));
}

// --- Drag-to-reorder -------------------------------------------------------

void XplorerToolbarView::WriteDragDataForView(views::View* sender,
                                              const gfx::Point& press_pt,
                                              ui::OSExchangeData* data) {
  const int index = PillIndexForView(sender);
  if (index < 0 || static_cast<size_t>(index) >= pills_.size()) {
    return;
  }
  // Carry the dragged pill id; ids are stable across the reorder, unlike a
  // positional index.
  data->SetString(base::UTF8ToUTF16(pills_[index].id));
}

int XplorerToolbarView::GetDragOperationsForView(views::View* sender,
                                                 const gfx::Point& p) {
  return ui::DragDropTypes::DRAG_MOVE;
}

bool XplorerToolbarView::CanStartDragForView(views::View* sender,
                                             const gfx::Point& press_pt,
                                             const gfx::Point& p) {
  return View::ExceededDragThreshold(p - press_pt);
}

bool XplorerToolbarView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = ui::OSExchangeData::STRING;
  return true;
}

bool XplorerToolbarView::AreDropTypesRequired() {
  return true;
}

bool XplorerToolbarView::CanDrop(const ui::OSExchangeData& data) {
  return data.HasString();
}

int XplorerToolbarView::OnDragUpdated(const ui::DropTargetEvent& event) {
  const int idx = ComputeDropIndex(event.location().x());
  if (idx != drop_index_) {
    drop_index_ = idx;
    SchedulePaint();
  }
  return ui::DragDropTypes::DRAG_MOVE;
}

void XplorerToolbarView::OnDragExited() {
  if (drop_index_ != -1) {
    drop_index_ = -1;
    SchedulePaint();
  }
}

views::View::DropCallback XplorerToolbarView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  const int target = ComputeDropIndex(event.location().x());
  const std::optional<std::u16string> dragged = event.data().GetString();
  drop_index_ = -1;
  SchedulePaint();
  return base::BindOnce(
      &XplorerToolbarView::PerformDrop, weak_factory_.GetWeakPtr(),
      base::UTF16ToUTF8(dragged.value_or(std::u16string())), target);
}

int XplorerToolbarView::ComputeDropIndex(int x) const {
  for (size_t i = 0; i < pill_buttons_.size(); ++i) {
    const views::View* btn = pill_buttons_[i].main;
    if (!btn) {
      continue;
    }
    const gfx::Rect b = btn->bounds();
    if (x < b.x() + b.width() / 2) {
      return static_cast<int>(i);
    }
  }
  return static_cast<int>(pill_buttons_.size());
}

std::vector<base::DictValue> XplorerToolbarView::SerializePills() const {
  std::vector<base::DictValue> out;
  out.reserve(pills_.size());
  for (const ToolbarPill& pill : pills_) {
    base::DictValue dict;
    dict.Set("id", pill.id);
    dict.Set("label", pill.label);
    dict.Set("href", pill.href);
    dict.Set("icon", pill.icon);
    dict.Set("enabled", true);
    if (pill.is_home) {
      dict.Set("isHome", true);
    }
    if (!pill.children.empty()) {
      base::ListValue kids;
      for (const ToolbarChild& child : pill.children) {
        base::DictValue cd;
        cd.Set("label", child.label);
        cd.Set("href", child.href);
        kids.Append(std::move(cd));
      }
      dict.Set("children", std::move(kids));
    }
    out.push_back(std::move(dict));
  }
  return out;
}

void XplorerToolbarView::PerformDrop(
    std::string dragged_id,
    int target_index,
    const ui::DropTargetEvent& event,
    ui::mojom::DragOperation& output_drag_op,
    std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
  output_drag_op = ui::mojom::DragOperation::kNone;
  if (dragged_id.empty()) {
    return;
  }
  int from = -1;
  for (size_t i = 0; i < pills_.size(); ++i) {
    if (pills_[i].id == dragged_id) {
      from = static_cast<int>(i);
      break;
    }
  }
  if (from < 0) {
    return;
  }
  int to = std::clamp(target_index, 0, static_cast<int>(pills_.size()));
  // Removing |from| shifts later items left by one.
  if (to > from) {
    --to;
  }
  output_drag_op = ui::mojom::DragOperation::kMove;
  if (to == from) {
    return;  // No change.
  }
  ToolbarPill moved = std::move(pills_[from]);
  pills_.erase(pills_.begin() + from);
  pills_.insert(pills_.begin() + to, std::move(moved));
  // Persist the new order; this notifies subscribers, which calls Reload() and
  // rebuilds the strip from the freshly written config.
  grok_companion::SetToolbarPillConfigs(SerializePills());
}

void XplorerToolbarView::OnPaint(gfx::Canvas* canvas) {
  views::AccessiblePaneView::OnPaint(canvas);
  if (drop_index_ < 0 || pill_buttons_.empty()) {
    return;
  }
  int x = kLeadingMargin;
  if (drop_index_ == 0) {
    const views::View* first = pill_buttons_.front().main;
    x = first ? first->bounds().x() - kButtonSpacing / 2 : kLeadingMargin;
  } else if (static_cast<size_t>(drop_index_) >= pill_buttons_.size()) {
    const views::View* last = pill_buttons_.back().main;
    x = last ? last->bounds().right() + kButtonSpacing / 2 : kLeadingMargin;
  } else {
    const views::View* btn = pill_buttons_[drop_index_].main;
    x = btn ? btn->bounds().x() - kButtonSpacing / 2 : kLeadingMargin;
  }
  const SkColor color =
      GetColorProvider() ? GetColorProvider()->GetColor(kColorToolbarButtonIcon)
                         : SK_ColorGRAY;
  canvas->FillRect(gfx::Rect(x - 1, 4, 2, std::max(0, height() - 8)), color);
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
