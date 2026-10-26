#!/usr/bin/env python3
"""Applies Xplorer's integration edits to a Chromium checkout.

Idempotent: each edit checks for its marker before inserting. Anchors on
long-lived symbols (PreMainMessageLoopRun, chrome/browser deps block) so it
survives upstream churn better than context diffs.
"""
import re
import sys
from pathlib import Path
import subprocess

MARKER = "// XPLORER"

# Always write LF. Path.write_text's default newline translation turns every
# edited file CRLF on Windows; git (attr eol=lf) then sees those files as
# "clean" while their bytes differ from checkout state, so reverts skip them
# and stale applied content survives forever (the NUC vtgh.h ANCHOR failure —
# 137 poisoned files). One hook here covers all 31 write_text call sites.
_orig_write_text = Path.write_text


def _write_text_lf(self, data, encoding=None, errors=None, newline=None):
    try:
        return _orig_write_text(self, data, encoding=encoding, errors=errors,
                                newline="\n" if newline is None else newline)
    except TypeError:  # Python < 3.10 lacks the newline kwarg — write bytes.
        return self.write_bytes(data.encode(encoding or "utf-8"))


Path.write_text = _write_text_lf


def edit(path: Path, anchor: str, insertion: str, before: bool = False):
    text = path.read_text()
    # Idempotency: skip if the full insertion is already present verbatim.
    if insertion in text:
        print(f"  skip (already applied): {path}")
        return
    # Second-chance idempotency: when edits to the same region interleave (one
    # edit splices lines INTO another's inserted block), the verbatim check
    # fails on re-apply even though the content is applied — and the anchor is
    # already consumed, so re-running dies with ANCHOR NOT FOUND (the vtgh.h
    # double-apply failure). If every line this edit ADDS (insertion minus
    # anchor lines) is already in the file, treat it as applied.
    anchor_lines = set(anchor.splitlines())
    added = [l for l in insertion.splitlines()
             if l.strip() and l not in anchor_lines]
    if added and all(l in text for l in added):
        print(f"  skip (already applied, interleaved): {path}")
        return
    idx = text.find(anchor)
    if idx < 0:
        sys.exit(f"ANCHOR NOT FOUND in {path}: {anchor!r} — upstream moved; "
                 f"update apply_integration.py")
    # When the insertion restates the anchor's first OR last line, it is a
    # rewritten version of the anchored block → replace the anchor with it.
    # Otherwise the insertion is purely additive → splice it before/after the
    # anchor. (Matching the last line catches rewrites whose opening line
    # changes but which end on the same statement, e.g. a trailing `return`.)
    a_lines = anchor.strip().splitlines()
    i_lines = insertion.strip().splitlines()
    if a_lines[0] == i_lines[0] or a_lines[-1] == i_lines[-1]:
        new_text = text[:idx] + insertion + text[idx + len(anchor):]
    else:
        pos = idx if before else idx + len(anchor)
        new_text = text[:pos] + insertion + text[pos:]
    path.write_text(new_text)
    print(f"  edited: {path}")


def rebrand_grd_strings(path: Path):
    """Replace the hardcoded "Chromium" app name with "Xplorer" in a grit
    strings file (.grd/.grdp), preserving the legal "Chromium Authors" copyright.
    Skips the google_chrome branded variants (not compiled in our Chromium build)
    and — as a safety net — any file whose <message name="…"> resource IDs contain
    "Chromium" (replacing those would break the build). Idempotent: once only the
    copyright keeps "Chromium", re-running is a no-op."""
    if not path.exists() or "google_chrome" in path.name:
        return
    g = path.read_text()
    if re.search(r'name="[^"]*Chromium', g):
        print(f"  skip (Chromium in resource IDs): {path.name}")
        return
    if g.count("Chromium") <= g.count("Chromium Authors"):
        return
    g = g.replace("Chromium Authors", "\x00A\x00")
    g = g.replace("Chromium", "Xplor")
    g = g.replace("\x00A\x00", "Chromium Authors")
    path.write_text(g)
    print(f"  rebranded grd: {path.name}")


def patch_xplorer_settings_access(src: Path):
    """Xplorer settings in app menu, chrome://settings nav, and command handler."""
    cmd_ids = src / "chrome/app/chrome_command_ids.h"
    edit(
        cmd_ids,
        "#define IDC_CHROME_ENTERPRISE_RELEASE_NOTES 40305",
        "#define IDC_CHROME_ENTERPRISE_RELEASE_NOTES 40305\n"
        "#define IDC_XPLORER_SETTINGS 40306  // XPLORER",
    )

    grdp = src / "chrome/app/settings_chromium_strings.grdp"
    # Self-heal: strip ANY existing IDS_XPLORER_SETTINGS message(s) before adding
    # one. A plain idempotent insert is defeated when the message TEXT changes
    # (e.g. "Xplorer settings" -> "Xplor settings"): the "already-present" check
    # misses the old wording and inserts a second copy, so a build tree that has
    # an older copy committed in ends up with a grit DuplicateKey. Stripping
    # first guarantees exactly one regardless of prior wording or tree state.
    _g = grdp.read_text()
    _g = re.sub(
        r'[ \t]*<message name="IDS_XPLORER_SETTINGS"[\s\S]*?</message>\n*',
        "", _g)
    # Also heal any comment duplication a previous strip+re-add cycle left
    # (the old insertion restated the anchor comment; re-applying could glue
    # "<!-- About Page -->  <!-- About Page -->" onto one line).
    _g = re.sub(r"(  <!-- About Page -->)([ \t]*<!-- About Page -->)+",
                r"\1", _g)
    grdp.write_text(_g)
    # Purely additive (before the anchor, which is NOT restated) — re-runnable.
    edit(
        grdp,
        "  <!-- About Page -->",
        '  <message name="IDS_XPLORER_SETTINGS" '
        'desc="App menu item to open Xplor companion settings" '
        'translateable="false">\n'
        "    Xplor settings\n"
        "  </message>\n\n",
        before=True,
    )

    app_menu = src / "chrome/browser/ui/toolbar/app_menu_model.cc"
    edit(
        app_menu,
        "  AddItemWithStringIdAndVectorIcon(\n"
        "      this, IDC_OPTIONS, IDS_SETTINGS,\n"
        "      features::IsRoundedIconsEnabled() ? kSettingsIcon : kSettingsMenuOldIcon);\n",
        "  AddItemWithStringIdAndVectorIcon(\n"
        "      this, IDC_OPTIONS, IDS_SETTINGS,\n"
        "      features::IsRoundedIconsEnabled() ? kSettingsIcon : kSettingsMenuOldIcon);\n\n"
        "  // XPLORER: companion settings (bookmarks, models, Grok defaults).\n"
        "  AddItemWithStringId(IDC_XPLORER_SETTINGS, IDS_XPLORER_SETTINGS);\n",
    )

    bcc = src / "chrome/browser/ui/browser_command_controller.cc"
    edit(
        bcc,
        '#include "chrome/browser/ui/browser_commands.h"',
        '#include "chrome/browser/ui/browser_commands.h"\n'
        '#include "chrome/browser/ui/views/xplorer/xplorer_settings_nav.h"  // XPLORER',
    )
    edit(
        bcc,
        "    case IDC_OPTIONS:\n"
        "      ShowSettings(webui::GetBrowserForOpeningWebUi(browser_));\n"
        "      break;",
        "    case IDC_OPTIONS:\n"
        "      ShowSettings(webui::GetBrowserForOpeningWebUi(browser_));\n"
        "      break;\n"
        "    case IDC_XPLORER_SETTINGS:  // XPLORER\n"
        "      xplorer::OpenXplorerSettings(browser_);\n"
        "      break;",
    )

    settings_menu = (
        src / "chrome/browser/resources/settings/settings_menu/settings_menu.html"
    )
    edit(
        settings_menu,
        '        <a role="menuitem" id="about-menu" href="/help"\n'
        '            class="cr-nav-menu-item">',
        '        <a role="menuitem" id="xplorer-settings-link" class="cr-nav-menu-item"\n'
        '            href="http://127.0.0.1:9334/settings" target="_blank"\n'
        '            on-click="onLinkClick_"\n'
        '            title="Bookmarks, models, and Grok defaults">\n'
        '          <cr-icon icon="settings:settings"></cr-icon>\n'
        '          <span>Xplor settings</span>\n'
        '          <div class="cr-icon icon-external"></div>\n'
        '          <cr-ripple></cr-ripple>\n'
        '        </a>\n'
        '        <a role="menuitem" id="about-menu" href="/help"\n'
        '            class="cr-nav-menu-item">',
    )


def patch_soft_tab_pills(src: Path):
    """Arc/Dia: vertical tab rows paint as soft inset pills, not Chrome cards."""
    path = src / "chrome/browser/ui/views/tabs/vertical_tab_style_views.cc"
    text = path.read_text()
    if "XPLORER: Arc soft pill" in text:
        print(f"  skip (already applied): {path}")
        return

    def swap(old: str, new: str, label: str):
        nonlocal text
        if old not in text:
            sys.exit(f"ANCHOR NOT FOUND in {path} ({label})")
        text = text.replace(old, new, 1)

    swap(
        '#include "chrome/browser/ui/layout_constants.h"\n',
        '#include "chrome/browser/ui/color/chrome_color_id.h"  // XPLORER\n'
        '#include "chrome/browser/ui/layout_constants.h"\n',
        "layout_constants include",
    )
    swap(
        '#include "ui/gfx/canvas.h"\n',
        '#include "ui/gfx/canvas.h"\n'
        '#include "ui/gfx/color_utils.h"  // XPLORER\n',
        "canvas include",
    )
    swap(
        "  if (flags.render_units == TabStyle::RenderUnits::kPixels) {\n"
        "    bounds.Scale(scale);\n"
        "  }\n"
        "  const SkScalar scaled_corner_radius =\n",
        "  if (flags.render_units == TabStyle::RenderUnits::kPixels) {\n"
        "    bounds.Scale(scale);\n"
        "  }\n"
        "  // XPLORER: Arc soft pill — float the fill inside the row so tabs don't\n"
        "  // read as stacked Chrome cards. Pinned squares stay full-bleed.\n"
        "  if (!delegate_->IsPinned()) {\n"
        "    const float inset_scale =\n"
        "        flags.render_units == TabStyle::RenderUnits::kPixels ? scale : 1.f;\n"
        "    bounds.Inset(gfx::InsetsF::VH(2.f * inset_scale, 2.f * inset_scale));\n"
        "  }\n"
        "  const SkScalar scaled_corner_radius =\n",
        "GetPath bounds",
    )
    swap(
        "SkColor VerticalTabStyleViews::GetCurrentTabBackgroundColor(\n"
        "    TabStyle::TabSelectionState selection_state) const {\n"
        "  const bool frame_glass = delegate_->IsGlassFrame();\n"
        "  return tab_style()->GetCurrentTabBackgroundColor(\n"
        "      selection_state, delegate_->IsHoverAnimationActive(),\n"
        "      delegate_->GetHoverAnimationValue(),\n"
        "      delegate_->GetView()->GetWidget()\n"
        "          ? delegate_->GetView()->GetWidget()->ShouldPaintAsActive()\n"
        "          : true,\n"
        "      frame_glass, delegate_->GetView()->GetColorProvider());\n"
        "}\n",
        "SkColor VerticalTabStyleViews::GetCurrentTabBackgroundColor(\n"
        "    TabStyle::TabSelectionState selection_state) const {\n"
        "  // XPLORER: Arc soft pill. A solid active card (kColorSysBase) fights the\n"
        "  // sidebar. Paint a quiet ink wash over the sidebar instead.\n"
        "  const ui::ColorProvider* colors = delegate_->GetView()->GetColorProvider();\n"
        "  const bool frame_active = delegate_->GetView()->GetWidget()\n"
        "                                ? delegate_->GetView()->GetWidget()\n"
        "                                      ->ShouldPaintAsActive()\n"
        "                                : true;\n"
        "  if (!colors) {\n"
        "    return tab_style()->GetCurrentTabBackgroundColor(\n"
        "        selection_state, delegate_->IsHoverAnimationActive(),\n"
        "        delegate_->GetHoverAnimationValue(), frame_active,\n"
        "        delegate_->IsGlassFrame(), nullptr);\n"
        "  }\n"
        "  const SkColor sidebar = colors->GetColor(\n"
        "      frame_active ? kColorTabBackgroundInactiveFrameActive\n"
        "                   : kColorTabBackgroundInactiveFrameInactive);\n"
        "  const bool dark = color_utils::GetRelativeLuminance(sidebar) < 0.4f;\n"
        "  const SkColor ink = dark ? SK_ColorWHITE : SK_ColorBLACK;\n"
        "  float rest = 0.f;\n"
        "  float hover = dark ? 0.08f : 0.045f;\n"
        "  if (selection_state == TabStyle::TabSelectionState::kActive) {\n"
        "    rest = dark ? 0.16f : 0.09f;\n"
        "    hover = dark ? 0.22f : 0.13f;\n"
        "  } else if (selection_state == TabStyle::TabSelectionState::kSelected) {\n"
        "    rest = dark ? 0.11f : 0.06f;\n"
        "    hover = dark ? 0.16f : 0.09f;\n"
        "  }\n"
        "  const float t = delegate_->IsHoverAnimationActive()\n"
        "                      ? static_cast<float>(delegate_->GetHoverAnimationValue())\n"
        "                      : 0.f;\n"
        "  return color_utils::AlphaBlend(ink, sidebar, rest + (hover - rest) * t);\n"
        "}\n",
        "GetCurrentTabBackgroundColor",
    )
    swap(
        "SkScalar VerticalTabStyleViews::GetCornerRadius() const {\n"
        "  return SkIntToScalar(\n"
        "      GetLayoutConstant(LayoutConstant::kVerticalTabCornerRadius) +\n"
        "      (delegate_->IsSplit() ? delegate_->GetView()->GetInsets().height() : 0));\n"
        "}\n",
        "SkScalar VerticalTabStyleViews::GetCornerRadius() const {\n"
        "  // XPLORER: Arc soft pill. Chrome's 8dp card corner stays boxy once the\n"
        "  // fill is inset; 12dp reads as a pill on the shorter row.\n"
        "  return SkIntToScalar(\n"
        "      12 +\n"
        "      (delegate_->IsSplit() ? delegate_->GetView()->GetInsets().height() : 0));\n"
        "}\n",
        "GetCornerRadius",
    )
    path.write_text(text)
    print(f"  edited: {path}")


def patch_hide_top_toolbar(src: Path):
    """Arc: the address field lives above the sidebar, not across the page."""
    path = src / "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc"
    text = path.read_text()
    if ("XPLORER: address field lives in the sidebar" in text or
            "XPLORER: a compact address strip" in text):
        print(f"  skip (already applied): {path.name}")
        return
    old = "  const bool toolbar_visible = delegate().IsToolbarVisible();\n"
    new = (
        "  // XPLORER: address field lives in the sidebar. Hide the Chrome\n"
        "  // toolbar across the top of the page when vertical tabs are on.\n"
        "  const bool toolbar_visible = delegate().IsToolbarVisible() &&\n"
        "      layout_data_->tab_strip_type != TabStripType::kVertical;\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (hide top toolbar)")
    path.write_text(text.replace(old, new, 1))
    print(f"  edited: {path}")


def patch_skip_crashed_session_bubble(src: Path):
    """Hidden toolbar has no app-menu anchor. Showing the bubble aborts."""
    path = src / "chrome/browser/ui/views/session_crashed_bubble_view.cc"
    text = path.read_text()
    if "XPLORER: no app menu to anchor the restore bubble" in text:
        print(f"  skip (already applied): {path.name}")
        return
    old = (
        "  auto* control = browser_view->toolbar_button_provider()->GetAppMenuControl();\n"
        "  views::BubbleAnchor anchor =\n"
        "      control ? control->GetAnchor() : views::BubbleAnchor();\n"
    )
    new = (
        "  auto* control = browser_view->toolbar_button_provider()->GetAppMenuControl();\n"
        "  // XPLORER: no app menu to anchor the restore bubble.\n"
        "  if (!control || !control->IsDrawn()) {\n"
        "    return nullptr;\n"
        "  }\n"
        "  views::BubbleAnchor anchor = control->GetAnchor();\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (session crashed bubble)")
    path.write_text(text.replace(old, new, 1))
    print(f"  edited: {path}")


def patch_sidebar_location_focus(src: Path):
    """Cmd-L focuses the sidebar field once the top toolbar is hidden."""
    view = src / "chrome/browser/ui/views/frame/browser_view.cc"
    print(f"  skip (address is the top strip): {view.name}")

    commands = src / "chrome/browser/ui/browser_command_controller.cc"
    text = commands.read_text()
    if "XPLORER: sidebar address field is the location bar" not in text:
        old = (
            "    case IDC_FOCUS_LOCATION:\n"
            "      if (!window()->IsLocationBarVisible()) {\n"
            "        break;\n"
            "      }\n"
            "      base::RecordAction(base::UserMetricsAction(\"Accel_Focus_Location\"));\n"
            "      FocusLocationBar(browser_);\n"
            "      break;\n"
        )
        new = (
            "    case IDC_FOCUS_LOCATION:\n"
            "      // XPLORER: sidebar address field is the location bar when\n"
            "      // the top toolbar is hidden. SetFocusToLocationBar no-ops\n"
            "      // if neither field is available.\n"
            "      base::RecordAction(base::UserMetricsAction(\"Accel_Focus_Location\"));\n"
            "      FocusLocationBar(browser_);\n"
            "      break;\n"
        )
        if old not in text:
            sys.exit(f"ANCHOR NOT FOUND in {commands} (focus location)")
        commands.write_text(text.replace(old, new, 1))
        print(f"  edited: {commands}")
    else:
        print(f"  skip (already applied): {commands.name}")


def patch_enable_glass_frame(src: Path):
    """Arc window: translucent sidebar with the system blur behind it."""
    path = src / "ui/base/ui_base_features.cc"
    text = path.read_text()
    old = "BASE_FEATURE(kGlassFrame, base::FEATURE_DISABLED_BY_DEFAULT);\n"
    new = (
        "// XPLORER: translucent window with system blur behind the sidebar.\n"
        "BASE_FEATURE(kGlassFrame, base::FEATURE_ENABLED_BY_DEFAULT);\n"
    )
    if "XPLORER: translucent window" in text:
        print(f"  skip (already applied): {path.name}")
        return
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (glass frame)")
    path.write_text(text.replace(old, new, 1))
    print(f"  edited: {path}")


def patch_sidebar_plane(src: Path):
    """Dia/Arc: the sidebar is its own flat plane, not Chrome's window frame."""
    path = src / "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc"
    text = path.read_text()
    if "XPLORER: sidebar plane" in text:
        print(f"  skip (already applied): {path}")
        return
    old = (
        "  SetBackground(std::make_unique<CustomCornersBackground>(\n"
        "      *this, *browser_view,\n"
        "      /*primary_color=*/CustomCornersBackground::FrameTheme(),\n"
        "      /*corner_color=*/CustomCornersBackground::ToolbarTheme()));\n"
    )
    new = (
        "  // XPLORER: sidebar plane. Chrome paints this with the window frame,\n"
        "  // which is why vertical tabs still look like Chrome. Dia and Arc use\n"
        "  // a flat neutral surface that is separate from the page.\n"
        "  SetBackground(std::make_unique<CustomCornersBackground>(\n"
        "      *this, *browser_view,\n"
        "      /*primary_color=*/ui::kColorSysSurface3,\n"
        "      /*corner_color=*/ui::kColorSysSurface3));\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (sidebar plane)")
    path.write_text(text.replace(old, new, 1))
    print(f"  edited: {path}")


def patch_quiet_new_tab_row(src: Path):
    """Arc/Dia: the vertical new-tab control is a quiet labeled row, not a chip."""
    header = src / "chrome/browser/ui/views/tabs/shared/new_tab_button.h"
    htext = header.read_text()
    if "SetArcQuietRow" not in htext:
        old = (
            "  // views::View:\n"
            "  void OnMouseEvent(ui::MouseEvent* event) override;\n"
            "\n"
            " private:\n"
            "  std::unique_ptr<views::ActionViewController> action_view_controller_;\n"
            "\n"
            "  raw_ptr<BrowserWindowInterface> browser_;\n"
        )
        new = (
            "  // views::View:\n"
            "  void OnMouseEvent(ui::MouseEvent* event) override;\n"
            "  void OnPaintBackground(gfx::Canvas* canvas) override;\n"
            "\n"
            "  // XPLORER: Arc quiet new-tab row. Rest is clear; hover is a wash.\n"
            "  void SetArcQuietRow(bool quiet);\n"
            "\n"
            " private:\n"
            "  std::unique_ptr<views::ActionViewController> action_view_controller_;\n"
            "\n"
            "  raw_ptr<BrowserWindowInterface> browser_;\n"
            "  bool arc_quiet_row_ = false;  // XPLORER\n"
        )
        if old not in htext:
            sys.exit(f"ANCHOR NOT FOUND in {header} (NewTabButton quiet row)")
        header.write_text(htext.replace(old, new, 1))
        print(f"  edited: {header}")
    else:
        print(f"  skip (already applied): {header}")

    cc = src / "chrome/browser/ui/views/tabs/shared/new_tab_button.cc"
    text = cc.read_text()
    if "XPLORER: Arc quiet new-tab row" not in text:
        old_inc = '#include "ui/views/view_class_properties.h"\n'
        new_inc = (
            '#include "ui/views/view_class_properties.h"\n'
            '#include "cc/paint/paint_flags.h"  // XPLORER\n'
            '#include "chrome/browser/ui/color/chrome_color_id.h"  // XPLORER\n'
            '#include "third_party/skia/include/core/SkColor.h"  // XPLORER\n'
            '#include "ui/color/color_provider.h"  // XPLORER\n'
            '#include "ui/gfx/canvas.h"  // XPLORER\n'
            '#include "ui/gfx/color_utils.h"  // XPLORER\n'
            '#include "ui/gfx/geometry/rect_f.h"  // XPLORER\n'
            '#include "ui/views/widget/widget.h"  // XPLORER\n'
        )
        if old_inc not in text:
            sys.exit(f"ANCHOR NOT FOUND in {cc} (includes)")
        text = text.replace(old_inc, new_inc, 1)
        old_tail = (
            "  TabStripFlatEdgeButton::OnMouseEvent(event);\n"
            "}\n"
            "\n"
            "BEGIN_METADATA(NewTabButton)\n"
        )
        new_tail = (
            "  TabStripFlatEdgeButton::OnMouseEvent(event);\n"
            "}\n"
            "\n"
            "void NewTabButton::SetArcQuietRow(bool quiet) {\n"
            "  if (arc_quiet_row_ == quiet) {\n"
            "    return;\n"
            "  }\n"
            "  arc_quiet_row_ = quiet;\n"
            "  SchedulePaint();\n"
            "}\n"
            "\n"
            "void NewTabButton::OnPaintBackground(gfx::Canvas* canvas) {\n"
            "  if (!arc_quiet_row_) {\n"
            "    TabStripFlatEdgeButton::OnPaintBackground(canvas);\n"
            "    return;\n"
            "  }\n"
            "  // XPLORER: Arc quiet new-tab row. A filled Chrome chip fights the\n"
            "  // sidebar. Rest stays clear; hover and press are a soft ink wash.\n"
            "  const views::Button::ButtonState state = GetState();\n"
            "  if (state != views::Button::STATE_HOVERED &&\n"
            "      state != views::Button::STATE_PRESSED) {\n"
            "    return;\n"
            "  }\n"
            "  const ui::ColorProvider* colors = GetColorProvider();\n"
            "  if (!colors) {\n"
            "    return;\n"
            "  }\n"
            "  const bool frame_active =\n"
            "      GetWidget() && GetWidget()->ShouldPaintAsActive();\n"
            "  const SkColor sidebar = colors->GetColor(\n"
            "      frame_active ? kColorTabBackgroundInactiveFrameActive\n"
            "                   : kColorTabBackgroundInactiveFrameInactive);\n"
            "  const bool dark = color_utils::GetRelativeLuminance(sidebar) < 0.4f;\n"
            "  const SkColor ink = dark ? SK_ColorWHITE : SK_ColorBLACK;\n"
            "  const float alpha = state == views::Button::STATE_PRESSED\n"
            "                          ? (dark ? 0.16f : 0.10f)\n"
            "                          : (dark ? 0.10f : 0.06f);\n"
            "  cc::PaintFlags flags;\n"
            "  flags.setAntiAlias(true);\n"
            "  flags.setStyle(cc::PaintFlags::kFill_Style);\n"
            "  flags.setColor(color_utils::AlphaBlend(ink, sidebar, alpha));\n"
            "  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 10.f, flags);\n"
            "}\n"
            "\n"
            "BEGIN_METADATA(NewTabButton)\n"
        )
        if old_tail not in text:
            sys.exit(f"ANCHOR NOT FOUND in {cc} (OnMouseEvent tail)")
        text = text.replace(old_tail, new_tail, 1)
        cc.write_text(text)
        print(f"  edited: {cc}")
    else:
        print(f"  skip (already applied): {cc}")

    bottom = src / (
        "chrome/browser/ui/views/tabs/vertical/"
        "vertical_tab_strip_bottom_container.cc"
    )
    btext = bottom.read_text()
    if "XPLORER: Arc quiet new-tab row" not in btext:
        old_add = (
            "  new_tab_button_ = AddChildView(std::move(new_tab_button));\n"
        )
        new_add = (
            "  new_tab_button_ = AddChildView(std::move(new_tab_button));\n"
            "  // XPLORER: Arc quiet new-tab row — clear fill, not a Chrome chip.\n"
            "  static_cast<shared::NewTabButton*>(new_tab_button_.get())\n"
            "      ->SetArcQuietRow(true);\n"
        )
        if old_add not in btext:
            sys.exit(f"ANCHOR NOT FOUND in {bottom} (AddChildView)")
        btext = btext.replace(old_add, new_add, 1)
        old_insets = (
            "  new_tab_button_->SetInsets(GetLayoutInsets(\n"
            "      collapsed ? LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_COLLAPSED\n"
            "                : LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_UNCOLLAPSED));\n"
            "}\n"
        )
        new_insets = (
            "  new_tab_button_->SetInsets(GetLayoutInsets(\n"
            "      collapsed ? LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_COLLAPSED\n"
            "                : LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_UNCOLLAPSED));\n"
            "  // XPLORER: Arc quiet new-tab row. Expanded sidebar reads \"New tab\".\n"
            "  // SetLabelText no-ops when the action already stored the string, so\n"
            "  // clear it first to force the visible-text update.\n"
            "  new_tab_button_->SetShouldShowLabel(!collapsed);\n"
            "  if (collapsed) {\n"
            "    new_tab_button_->SetText(std::u16string());\n"
            "    new_tab_button_->SetHorizontalAlignment(gfx::ALIGN_CENTER);\n"
            "  } else {\n"
            "    new_tab_button_->SetLabelText(std::u16string());\n"
            "    new_tab_button_->SetLabelText(u\"New tab\");\n"
            "  }\n"
            "}\n"
        )
        if old_insets not in btext:
            sys.exit(f"ANCHOR NOT FOUND in {bottom} (SetInsets)")
        btext = btext.replace(old_insets, new_insets, 1)
        bottom.write_text(btext)
        print(f"  edited: {bottom}")
    else:
        print(f"  skip (already applied): {bottom}")


def patch_hover_close(src: Path):
    """Arc/Dia: the vertical-tab close button shows on hover, not on the active row."""
    path = src / "chrome/browser/ui/views/tabs/common/tab_view_vertical_layout.cc"
    text = path.read_text()
    if "XPLORER: Arc hover close" in text:
        print(f"  skip (already applied): {path}")
        return
    old = "    return TabView().active_ || hovered_or_focused;\n"
    new = (
        "    // XPLORER: Arc hover close. The active row stays quiet; the X\n"
        "    // shows only while the pointer or focus is on the tab.\n"
        "    return hovered_or_focused;\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (hover close)")
    path.write_text(text.replace(old, new, 1))
    print(f"  edited: {path}")


def patch_quiet_tab_type(src: Path):
    """Arc/Dia: inactive vertical-tab titles recede; the active row stays strong."""
    layout = src / "chrome/browser/ui/views/tabs/common/tab_view_vertical_layout.cc"
    ltext = layout.read_text()
    if "XPLORER: Arc quiet type" not in ltext:
        old = (
            "void TabViewVerticalLayout::OnInstalled(views::View* host) {\n"
            "  TabView::LayoutManager::OnInstalled(host);\n"
        )
        new = (
            "void TabViewVerticalLayout::OnInstalled(views::View* host) {\n"
            "  TabView::LayoutManager::OnInstalled(host);\n"
            "  // XPLORER: Arc quiet type. A step smaller than the horizontal\n"
            "  // tab face, so the sidebar reads as a list, not a tab strip.\n"
            "  if (TabView().title_) {\n"
            "    TabView().title_->SetFontList(\n"
            "        TabView().title_->font_list().DeriveWithSizeDelta(-1));\n"
            "  }\n"
        )
        if old not in ltext:
            sys.exit(f"ANCHOR NOT FOUND in {layout} (quiet type)")
        layout.write_text(ltext.replace(old, new, 1))
        print(f"  edited: {layout}")
    else:
        print(f"  skip (already applied): {layout}")

    view = src / "chrome/browser/ui/views/tabs/common/tab_view.cc"
    vtext = view.read_text()
    if "XPLORER: Arc quiet type" not in vtext:
        old_inc = '#include "chrome/browser/ui/layout_constants.h"\n'
        new_inc = (
            '#include "chrome/browser/ui/color/chrome_color_id.h"  // XPLORER\n'
            '#include "chrome/browser/ui/layout_constants.h"\n'
            '#include "ui/gfx/color_utils.h"  // XPLORER\n'
        )
        if old_inc not in vtext:
            sys.exit(f"ANCHOR NOT FOUND in {view} (includes)")
        vtext = vtext.replace(old_inc, new_inc, 1)
        old_color = (
            "  TabStyle::TabColors colors = tab_styling()->CalculateTargetColors();\n"
            "  title_->SetEnabledColor(colors.foreground_color);\n"
        )
        new_color = (
            "  TabStyle::TabColors colors = tab_styling()->CalculateTargetColors();\n"
            "  SkColor foreground = colors.foreground_color;\n"
            "  // XPLORER: Arc quiet type. Inactive rows recede into the sidebar;\n"
            "  // the active title stays at full strength.\n"
            "  if (orientation_ == TabStripOrientation::kVertical && !IsActive() &&\n"
            "      GetColorProvider()) {\n"
            "    const SkColor sidebar = GetColorProvider()->GetColor(\n"
            "        IsFrameActive() ? kColorTabBackgroundInactiveFrameActive\n"
            "                        : kColorTabBackgroundInactiveFrameInactive);\n"
            "    foreground = color_utils::AlphaBlend(foreground, sidebar, 0.52f);\n"
            "  }\n"
            "  title_->SetEnabledColor(foreground);\n"
        )
        if old_color not in vtext:
            sys.exit(f"ANCHOR NOT FOUND in {view} (UpdateColors)")
        view.write_text(vtext.replace(old_color, new_color, 1))
        print(f"  edited: {view}")
    else:
        print(f"  skip (already applied): {view}")


def patch_floating_address_pill(src: Path):
    """Dia: a short capsule in the toolbar, not a Chrome bar across the window."""
    toolbar = src / "chrome/browser/ui/views/toolbar/toolbar_view.cc"
    text = toolbar.read_text()
    if ("XPLORER: floating address pill" in text or
            "XPLORER: compact top address" in text):
        print(f"  skip (already applied): {toolbar}")
    else:
        old = (
            "  if (location_bar_view_) {\n"
            "    location_bar_view_->SetProperty(views::kFlexBehaviorKey,\n"
            "                                    location_bar_flex_rule);\n"
            "    int omnibox_side = location_bar_margin;\n"
            "    if (const auto* vts =\n"
            "            tabs::VerticalTabStripStateController::From(browser_);\n"
            "        vts && vts->ShouldDisplayVerticalTabs()) {\n"
            "      // XPLORER: quiet toolbar scale. Pull the pill off the nav buttons.\n"
            "      omnibox_side = 16;\n"
            "    }\n"
            "    location_bar_view_->SetProperty(views::kMarginsKey,\n"
            "                                    gfx::Insets::VH(0, omnibox_side));\n"
            "  }\n"
        )
        new = (
            "  if (location_bar_view_) {\n"
            "    int omnibox_side = location_bar_margin;\n"
            "    views::FlexSpecification omnibox_flex = location_bar_flex_rule;\n"
            "    if (const auto* vts =\n"
            "            tabs::VerticalTabStripStateController::From(browser_);\n"
            "        vts && vts->ShouldDisplayVerticalTabs()) {\n"
            "      // XPLORER: floating address pill. A short capsule in the\n"
            "      // middle of the plane, not a bar that spans the window.\n"
            "      omnibox_side = 12;\n"
            "      constexpr int kPillWidth = 520;\n"
            "      const int pill_height =\n"
            "          GetLayoutConstant(LayoutConstant::kLocationBarHeight);\n"
            "      location_bar_view_->SetPreferredSize(\n"
            "          gfx::Size(kPillWidth, pill_height));\n"
            "      omnibox_flex = location_bar_flex_rule.WithAlignment(\n"
            "          views::LayoutAlignment::kCenter);\n"
            "    }\n"
            "    location_bar_view_->SetProperty(views::kFlexBehaviorKey,\n"
            "                                    omnibox_flex);\n"
            "    location_bar_view_->SetProperty(views::kMarginsKey,\n"
            "                                    gfx::Insets::VH(0, omnibox_side));\n"
            "  }\n"
        )
        if old not in text:
            sys.exit(f"ANCHOR NOT FOUND in {toolbar} (floating pill)")
        toolbar.write_text(text.replace(old, new, 1))
        print(f"  edited: {toolbar}")

    bar = src / "chrome/browser/ui/views/location_bar/location_bar_view.cc"
    btext = bar.read_text()
    if "XPLORER: floating address pill" in btext:
        print(f"  skip (already applied): {bar}")
        return
    # The quiet-omnibox edit adds this include, but it runs later. A host
    # that has not had that edit yet still has the upstream include.
    vts_inc = (
        '#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"'
        "  // XPLORER\n"
    )
    color_inc = '#include "ui/gfx/color_utils.h"  // XPLORER\n'
    if vts_inc not in btext:
        old_inc = '#include "chrome/browser/ui/layout_constants.h"\n'
        if old_inc not in btext:
            sys.exit(f"ANCHOR NOT FOUND in {bar} (include)")
        btext = btext.replace(old_inc, old_inc + vts_inc, 1)
    if color_inc not in btext:
        btext = btext.replace(vts_inc, vts_inc + color_inc, 1)
    old = (
        "    background_color_ = gfx::Tween::ColorValueBetween(opacity, normal, hovered);\n"
        "  }\n"
        "\n"
        "  SkColor border_color = SK_ColorTRANSPARENT;\n"
    )
    new = (
        "    background_color_ = gfx::Tween::ColorValueBetween(opacity, normal, hovered);\n"
        "  }\n"
        "\n"
        "  // XPLORER: floating address pill. The capsule needs its own fill\n"
        "  // or it disappears into the sidebar plane. Focus still matches\n"
        "  // the popup.\n"
        "  if (!is_caret_visible && !high_contrast && !input_in_progress) {\n"
        "    const auto* vts =\n"
        "        browser_\n"
        "            ? tabs::VerticalTabStripStateController::From(browser_.get())\n"
        "            : nullptr;\n"
        "    if (vts && vts->ShouldDisplayVerticalTabs()) {\n"
        "      const SkColor plane = color_provider->GetColor(ui::kColorSysSurface3);\n"
        "      const bool dark = color_utils::GetRelativeLuminance(plane) < 0.4f;\n"
        "      const float rest = dark ? 0.10f : 0.90f;\n"
        "      const float hover = dark ? 0.16f : 0.97f;\n"
        "      background_color_ = color_utils::AlphaBlend(\n"
        "          SK_ColorWHITE, plane,\n"
        "          rest + (hover - rest) * static_cast<float>(opacity));\n"
        "    }\n"
        "  }\n"
        "\n"
        "  SkColor border_color = SK_ColorTRANSPARENT;\n"
    )
    if old not in btext:
        sys.exit(f"ANCHOR NOT FOUND in {bar} (pill fill)")
    bar.write_text(btext.replace(old, new, 1))
    print(f"  edited: {bar}")


def patch_quiet_bookmark_star(src: Path):
    """Arc/Dia: the empty bookmark star is not drawn in the address pill."""
    path = src / (
        "chrome/browser/ui/views/bookmarks/bookmark_page_action_controller.cc"
    )
    text = path.read_text()
    if "XPLORER: quiet star" in text:
        print(f"  skip (already applied): {path}")
        return
    old_inc = '#include "chrome/browser/ui/tabs/contents_observing_tab_feature.h"\n'
    new_inc = (
        '#include "chrome/browser/ui/tabs/contents_observing_tab_feature.h"\n'
        '#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"'
        "  // XPLORER\n"
    )
    if old_inc not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (include)")
    text = text.replace(old_inc, new_inc, 1)
    old = (
        "void BookmarkPageActionController::URLStarredChanged(\n"
        "    content::WebContents* web_contents,\n"
        "    bool starred) {\n"
        "  SetStarred(starred);\n"
        "}\n"
    )
    new = (
        "void BookmarkPageActionController::URLStarredChanged(\n"
        "    content::WebContents* web_contents,\n"
        "    bool starred) {\n"
        "  SetStarred(starred);\n"
        "  // XPLORER: quiet star. Visibility depends on starred state.\n"
        "  UpdatePageActionVisibility();\n"
        "}\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (starred changed)")
    text = text.replace(old, new, 1)
    old = (
        "bool BookmarkPageActionController::ShouldShowPageAction() const {\n"
        "  const auto& url = tab().GetContents()->GetLastCommittedURL();\n"
        "  return browser_defaults::bookmarks_enabled &&\n"
        "         edit_bookmarks_enabled_.GetValue() && url.is_valid() && !IsNTPUrl(url);\n"
        "}\n"
    )
    new = (
        "bool BookmarkPageActionController::ShouldShowPageAction() const {\n"
        "  const auto& url = tab().GetContents()->GetLastCommittedURL();\n"
        "  if (!(browser_defaults::bookmarks_enabled &&\n"
        "        edit_bookmarks_enabled_.GetValue() && url.is_valid() &&\n"
        "        !IsNTPUrl(url))) {\n"
        "    return false;\n"
        "  }\n"
        "  // XPLORER: quiet star. An empty star is Chrome chrome. A filled\n"
        "  // star still shows when the page is bookmarked. Cmd-D still works.\n"
        "  const auto* helper =\n"
        "      BookmarkTabHelper::FromWebContents(tab().GetContents());\n"
        "  if (helper && !helper->is_starred()) {\n"
        "    const auto* browser = tab().GetBrowserWindowInterface();\n"
        "    const auto* vts =\n"
        "        browser ? tabs::VerticalTabStripStateController::From(browser)\n"
        "                : nullptr;\n"
        "    if (vts && vts->ShouldDisplayVerticalTabs()) {\n"
        "      return false;\n"
        "    }\n"
        "  }\n"
        "  return true;\n"
        "}\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (should show)")
    path.write_text(text.replace(old, new, 1))
    print(f"  edited: {path}")


def patch_quiet_omnibox_icon(src: Path):
    """Arc/Dia: the idle lock is not drawn in the address pill."""
    path = src / "chrome/browser/ui/views/location_bar/location_bar_view.cc"
    text = path.read_text()
    if "XPLORER: quiet omnibox" in text:
        print(f"  skip (already applied): {path}")
        return
    old_inc = '#include "chrome/browser/ui/layout_constants.h"\n'
    new_inc = (
        '#include "chrome/browser/ui/layout_constants.h"\n'
        '#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"'
        "  // XPLORER\n"
    )
    if old_inc not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (include)")
    text = text.replace(old_inc, new_inc, 1)
    old = (
        "  } else if (!ShouldChipOverrideLocationIcon()) {\n"
        "    location_icon_view_->SetVisible(true);\n"
        "    leading_decorations.AddDecoration(vertical_padding, location_height, false,\n"
        "                                      0, /*intra_item_padding=*/0, icon_left,\n"
        "                                      location_icon_view_);\n"
        "  } else {\n"
    )
    new = (
        "  } else if (!ShouldChipOverrideLocationIcon()) {\n"
        "    const auto* vts =\n"
        "        browser_\n"
        "            ? tabs::VerticalTabStripStateController::From(browser_.get())\n"
        "            : nullptr;\n"
        "    // XPLORER: quiet omnibox. The idle lock is Chrome chrome. A\n"
        "    // warning chip still shows when the page is not secure.\n"
        "    if (vts && vts->ShouldDisplayVerticalTabs()) {\n"
        "      location_icon_view_->SetVisible(false);\n"
        "    } else {\n"
        "      location_icon_view_->SetVisible(true);\n"
        "      leading_decorations.AddDecoration(\n"
        "          vertical_padding, location_height, false, 0,\n"
        "          /*intra_item_padding=*/0, icon_left, location_icon_view_);\n"
        "    }\n"
        "  } else {\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (idle lock)")
    path.write_text(text.replace(old, new, 1))
    print(f"  edited: {path}")


def patch_hide_idle_reload(src: Path):
    """Arc/Dia: no idle reload button. Stop still appears while a page loads."""
    path = src / "chrome/browser/ui/views/toolbar/reload_button.cc"
    text = path.read_text()
    if "XPLORER: hide idle reload" in text:
        print(f"  skip (already applied): {path}")
        return
    old = (
        "  visible_mode_ = mode;\n"
        "\n"
        "  UpdateCachedTooltipText();\n"
        "  OnPropertyChanged(&visible_mode_, views::PropertyEffects::kNone);\n"
        "}\n"
    )
    new = (
        "  visible_mode_ = mode;\n"
        "\n"
        "  // XPLORER: hide idle reload. The circle next to Back is Chrome\n"
        "  // chrome. Stop still appears while a page is loading. Reload\n"
        "  // stays on Cmd-R and in the app menu.\n"
        "  SetVisible(visible_mode_ == Mode::kStop);\n"
        "\n"
        "  UpdateCachedTooltipText();\n"
        "  OnPropertyChanged(&visible_mode_, views::PropertyEffects::kNone);\n"
        "}\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (SetVisibleMode)")
    path.write_text(text.replace(old, new, 1))
    print(f"  edited: {path}")


def patch_quiet_toolbar_scale(src: Path):
    """Arc/Dia: the address pill and nav buttons are shorter than Chrome's."""
    constants = src / "chrome/browser/ui/layout_constants.cc"
    ctext = constants.read_text()
    if "XPLORER: quiet toolbar scale" in ctext:
        print(f"  skip (already applied): {constants}")
    else:
        old = (
            "    case LayoutConstant::kToolbarButtonHeight:\n"
            "      return touch_ui ? 48 : 34;\n"
        )
        new = (
            "    case LayoutConstant::kToolbarButtonHeight:\n"
            "      // XPLORER: quiet toolbar scale.\n"
            "      return touch_ui ? 48 : 28;\n"
        )
        if old not in ctext:
            sys.exit(f"ANCHOR NOT FOUND in {constants} (button height)")
        ctext = ctext.replace(old, new, 1)
        old = (
            "    case LayoutConstant::kLocationBarHeight:\n"
            "      return touch_ui ? 36 : 34;\n"
        )
        new = (
            "    case LayoutConstant::kLocationBarHeight:\n"
            "      // XPLORER: quiet toolbar scale. A shorter pill.\n"
            "      return touch_ui ? 36 : 28;\n"
        )
        if old not in ctext:
            sys.exit(f"ANCHOR NOT FOUND in {constants} (location bar height)")
        constants.write_text(ctext.replace(old, new, 1))
        print(f"  edited: {constants}")

    toolbar = src / "chrome/browser/ui/views/toolbar/toolbar_view.cc"
    text = toolbar.read_text()
    if ("XPLORER: quiet toolbar scale" in text or
            "XPLORER: compact top address" in text):
        print(f"  skip (already applied): {toolbar}")
        return
    old = (
        "    location_bar_view_->SetProperty(views::kMarginsKey,\n"
        "                                    gfx::Insets::VH(0, location_bar_margin));\n"
    )
    new = (
        "    int omnibox_side = location_bar_margin;\n"
        "    if (const auto* vts =\n"
        "            tabs::VerticalTabStripStateController::From(browser_);\n"
        "        vts && vts->ShouldDisplayVerticalTabs()) {\n"
        "      // XPLORER: quiet toolbar scale. Pull the pill off the nav buttons.\n"
        "      omnibox_side = 16;\n"
        "    }\n"
        "    location_bar_view_->SetProperty(views::kMarginsKey,\n"
        "                                    gfx::Insets::VH(0, omnibox_side));\n"
    )
    count = text.count(old)
    if count == 0:
        sys.exit(f"ANCHOR NOT FOUND in {toolbar} (omnibox margin)")
    text = text.replace(old, new)
    old_touch = (
        "      location_bar_view_->SetProperty(views::kMarginsKey,\n"
        "                                      gfx::Insets::VH(0, location_bar_margin));\n"
    )
    new_touch = (
        "      int omnibox_side = location_bar_margin;\n"
        "      if (const auto* vts =\n"
        "              tabs::VerticalTabStripStateController::From(browser_);\n"
        "          vts && vts->ShouldDisplayVerticalTabs()) {\n"
        "        omnibox_side = 16;\n"
        "      }\n"
        "      location_bar_view_->SetProperty(views::kMarginsKey,\n"
        "                                      gfx::Insets::VH(0, omnibox_side));\n"
    )
    if old_touch in text:
        text = text.replace(old_touch, new_touch, 1)
    old = (
        "  auto* vts_controller = tabs::VerticalTabStripStateController::From(browser_);\n"
        "  if (contextual_tasks::IsContextualTasksUIEnabled() &&\n"
    )
    new = (
        "  auto* vts_controller = tabs::VerticalTabStripStateController::From(browser_);\n"
        "  if (vts_controller && vts_controller->ShouldDisplayVerticalTabs()) {\n"
        "    // XPLORER: quiet toolbar scale. Less air above and below the pill.\n"
        "    interior_margin.set_top(4);\n"
        "    interior_margin.set_bottom(4);\n"
        "  }\n"
        "  if (contextual_tasks::IsContextualTasksUIEnabled() &&\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {toolbar} (interior margin)")
    toolbar.write_text(text.replace(old, new, 1))
    print(f"  edited: {toolbar}")


def patch_page_card(src: Path):
    """Arc/Dia: the page is a rounded card, not flush with the window."""
    view = src / "chrome/browser/ui/views/frame/browser_view.cc"
    vtext = view.read_text()
    if "XPLORER: page card" in vtext:
        print(f"  skip (already applied): {view}")
    else:
        old = "  GetWidget()->SetBackgroundColor(kColorToolbar);\n"
        new = (
            "  // XPLORER: page card. The gap around the page is the same\n"
            "  // plane as the sidebar, not the Chrome toolbar color.\n"
            "  const auto* vts =\n"
            "      tabs::VerticalTabStripStateController::From(browser());\n"
            "  if (vts && vts->ShouldDisplayVerticalTabs()) {\n"
            "    GetWidget()->SetBackgroundColor(ui::kColorSysSurface3);\n"
            "  } else {\n"
            "    GetWidget()->SetBackgroundColor(kColorToolbar);\n"
            "  }\n"
        )
        if old not in vtext:
            sys.exit(f"ANCHOR NOT FOUND in {view} (widget background)")
        view.write_text(vtext.replace(old, new, 1))
        print(f"  edited: {view}")

    layout = src / (
        "chrome/browser/ui/views/frame/layout/"
        "browser_view_tabbed_layout_impl.cc"
    )
    ltext = layout.read_text()
    if "XPLORER: page card" in ltext:
        print(f"  skip (already applied): {layout}")
        return
    old = (
        "  auto& contents_layout =\n"
        "      layout.AddChild(views().multi_contents_view,\n"
        "                      gfx::Rect(content_left, params.visual_client_area.y(),\n"
        "                                content_right - content_left,\n"
        "                                params.visual_client_area.height()));\n"
    )
    new = (
        "  int content_top = params.visual_client_area.y();\n"
        "  int content_height = params.visual_client_area.height();\n"
        "  // XPLORER: page card. Inset the page so it sits on the chrome\n"
        "  // plane instead of filling the window like a Chrome tab.\n"
        "  if (layout_data_->tab_strip_type == TabStripType::kVertical &&\n"
        "      !is_fullscreen(layout_data_->window_state)) {\n"
        "    constexpr int kCardInset = 8;\n"
        "    constexpr int kCardLeading = 6;\n"
        "    content_top += kCardInset;\n"
        "    content_height = std::max(0, content_height - kCardInset * 2);\n"
        "    if (base::i18n::IsRTL()) {\n"
        "      content_right = std::max(content_left, content_right - kCardLeading);\n"
        "      content_left += kCardInset;\n"
        "    } else {\n"
        "      content_left += kCardLeading;\n"
        "      content_right = std::max(content_left, content_right - kCardInset);\n"
        "    }\n"
        "  }\n"
        "  auto& contents_layout =\n"
        "      layout.AddChild(views().multi_contents_view,\n"
        "                      gfx::Rect(content_left, content_top,\n"
        "                                std::max(0, content_right - content_left),\n"
        "                                content_height));\n"
    )
    if old not in ltext:
        sys.exit(f"ANCHOR NOT FOUND in {layout} (contents bounds)")
    ltext = ltext.replace(old, new, 1)
    old = (
        "    views().multi_contents_view->SetBackgroundRadii(content_corners);\n"
        "  }\n"
    )
    new = (
        "    views().multi_contents_view->SetBackgroundRadii(content_corners);\n"
        "  }\n"
        "  // XPLORER: page card. Round every corner. The glass path above only\n"
        "  // rounds the lower leading corner, and only sometimes.\n"
        "  if (layout_data_->tab_strip_type == TabStripType::kVertical &&\n"
        "      !is_fullscreen(layout_data_->window_state)) {\n"
        "    views().multi_contents_view->SetBackgroundRadii(\n"
        "        gfx::RoundedCornersF(12.f));\n"
        "  }\n"
    )
    if old not in ltext:
        sys.exit(f"ANCHOR NOT FOUND in {layout} (corner radii)")
    layout.write_text(ltext.replace(old, new, 1))
    print(f"  edited: {layout}")


def patch_toolbar_plane(src: Path):
    """Arc/Dia: the toolbar is the sidebar's plane, not a Chrome strip."""
    toolbar = src / "chrome/browser/ui/views/toolbar/toolbar_view.cc"
    text = toolbar.read_text()
    if "XPLORER: toolbar plane" in text:
        print(f"  skip (already applied): {toolbar}")
    else:
        old = (
            "  if (display_mode_ == DisplayMode::kNormal) {\n"
            "    SetBackground(std::make_unique<CustomCornersBackground>(\n"
            "        *this, *browser_view_,\n"
            "        /*primary_color=*/CustomCornersBackground::ToolbarTheme(),\n"
            "        /*corner_color=*/CustomCornersBackground::FrameTheme()));\n"
            "  } else if (display_mode_ == DisplayMode::kCustomTab) {\n"
        )
        new = (
            "  if (display_mode_ == DisplayMode::kNormal) {\n"
            "    const auto* vts =\n"
            "        tabs::VerticalTabStripStateController::From(browser_);\n"
            "    // XPLORER: toolbar plane. With vertical tabs the toolbar is\n"
            "    // the same flat surface as the sidebar. The omnibox is the\n"
            "    // only chrome on that plane.\n"
            "    if (vts && vts->ShouldDisplayVerticalTabs()) {\n"
            "      views::SetCascadingColorProviderColor(\n"
            "          this, views::kCascadingBackgroundColor,\n"
            "          ui::kColorSysSurface3);\n"
            "      SetBackground(std::make_unique<CustomCornersBackground>(\n"
            "          *this, *browser_view_,\n"
            "          /*primary_color=*/ui::kColorSysSurface3,\n"
            "          /*corner_color=*/ui::kColorSysSurface3));\n"
            "    } else {\n"
            "      SetBackground(std::make_unique<CustomCornersBackground>(\n"
            "          *this, *browser_view_,\n"
            "          /*primary_color=*/CustomCornersBackground::ToolbarTheme(),\n"
            "          /*corner_color=*/CustomCornersBackground::FrameTheme()));\n"
            "    }\n"
            "  } else if (display_mode_ == DisplayMode::kCustomTab) {\n"
        )
        if old not in text:
            sys.exit(f"ANCHOR NOT FOUND in {toolbar} (toolbar background)")
        toolbar.write_text(text.replace(old, new, 1))
        print(f"  edited: {toolbar}")

    layout = src / (
        "chrome/browser/ui/views/frame/layout/"
        "browser_view_tabbed_layout_impl.cc"
    )
    ltext = layout.read_text()
    if "XPLORER: toolbar plane" in ltext:
        print(f"  skip (already applied): {layout}")
        return
    old = (
        "  views().multi_contents_view->SetShouldShowTopSeparator(\n"
        "      separator_info.multi_contents_separator);\n"
    )
    new = (
        "  // XPLORER: toolbar plane. A hairline under the address bar makes\n"
        "  // the toolbar a Chrome strip again.\n"
        "  views().multi_contents_view->SetShouldShowTopSeparator(\n"
        "      separator_info.multi_contents_separator &&\n"
        "      layout_data_->tab_strip_type != TabStripType::kVertical);\n"
    )
    if old not in ltext:
        sys.exit(f"ANCHOR NOT FOUND in {layout} (top separator)")
    layout.write_text(ltext.replace(old, new, 1))
    print(f"  edited: {layout}")


def patch_quiet_toolbar_edge(src: Path):
    """Arc/Dia: no profile chip or divider on the toolbar's right edge."""
    path = src / "chrome/browser/ui/views/toolbar/toolbar_view.cc"
    text = path.read_text()
    if "XPLORER: quiet toolbar edge" in text:
        print(f"  skip (already applied): {path}")
        return
    old = (
        "  if (toolbar_divider) {\n"
        "    toolbar_divider_ = AddChildView(std::move(toolbar_divider));\n"
        "  }\n"
    )
    new = (
        "  if (toolbar_divider) {\n"
        "    toolbar_divider_ = AddChildView(std::move(toolbar_divider));\n"
        "    // XPLORER: quiet toolbar edge. The rule between extensions and\n"
        "    // Grok is Chrome chrome.\n"
        "    toolbar_divider_->SetVisible(false);\n"
        "  }\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (divider)")
    text = text.replace(old, new, 1)
    old = (
        "    bool show_avatar_toolbar_button =\n"
        "        AvatarToolbarButtonInterface::CanShowForProfile(browser_->GetProfile());\n"
        "    avatar_->SetVisible(show_avatar_toolbar_button);\n"
    )
    new = (
        "    // XPLORER: quiet toolbar edge. Arc keeps the profile out of the\n"
        "    // toolbar. The app menu still opens it.\n"
        "    avatar_->SetVisible(false);\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (avatar)")
    path.write_text(text.replace(old, new, 1))
    print(f"  edited: {path}")


def patch_quiet_toolbar_pins(src: Path):
    """Arc/Dia: the toolbar keeps Grok and drops Chrome Labs and tab search."""
    path = src / (
        "chrome/browser/ui/toolbar/pinned_toolbar/"
        "pinned_toolbar_actions_model.cc"
    )
    text = path.read_text()
    if "XPLORER: quiet toolbar" in text:
        print(f"  skip (already applied): {path}")
        return
    old = (
        "  // XPLORER: keep the Grok side-panel button pinned (always visible).\n"
        "  UpdatePinnedState(kActionSidePanelShowSearchCompanion, true);\n"
    )
    new = (
        "  // XPLORER: keep the Grok side-panel button pinned (always visible).\n"
        "  UpdatePinnedState(kActionSidePanelShowSearchCompanion, true);\n"
        "  // XPLORER: quiet toolbar. Chrome Labs and tab search are toolbar\n"
        "  // chrome. Grok stays. Tab search remains on the keyboard.\n"
        "  UpdatePinnedState(kActionShowChromeLabs, false);\n"
        "  UpdatePinnedState(kActionTabSearch, false);\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (grok pin)")
    text = text.replace(old, new, 1)
    old = (
        "  if (!pref_service_->GetBoolean(prefs::kPinnedChromeLabsMigrationComplete)) {\n"
        "    UpdatePinnedState(kActionShowChromeLabs, true);\n"
        "    pref_service_->SetBoolean(prefs::kPinnedChromeLabsMigrationComplete, true);\n"
        "  }\n"
    )
    new = (
        "  if (!pref_service_->GetBoolean(prefs::kPinnedChromeLabsMigrationComplete)) {\n"
        "    // XPLORER: quiet toolbar. Do not pin Chrome Labs.\n"
        "    pref_service_->SetBoolean(prefs::kPinnedChromeLabsMigrationComplete, true);\n"
        "  }\n"
    )
    if old not in text:
        sys.exit(f"ANCHOR NOT FOUND in {path} (labs migration)")
    path.write_text(text.replace(old, new, 1))
    print(f"  edited: {path}")


def patch_favorites_density(src: Path):
    """Arc/Dia: vertical tabs are a short favorites list, not a Chrome grid."""
    constants = src / "chrome/browser/ui/layout_constants.cc"
    ctext = constants.read_text()
    if "XPLORER: favorites density" in ctext:
        print(f"  skip (already applied): {constants}")
    else:
        old = (
            "    case LayoutConstant::kVerticalTabHeight:\n"
            "      return 30;\n"
            "    case LayoutConstant::kVerticalTabPinnedHeight:\n"
            "      return 32;\n"
        )
        new = (
            "    case LayoutConstant::kVerticalTabHeight:\n"
            "      // XPLORER: favorites density. Arc rows are shorter than\n"
            "      // Chrome's vertical tabs.\n"
            "      return 26;\n"
            "    case LayoutConstant::kVerticalTabPinnedHeight:\n"
            "      return 26;\n"
        )
        if old not in ctext:
            sys.exit(f"ANCHOR NOT FOUND in {constants} (tab height)")
        ctext = ctext.replace(old, new, 1)
        old = (
            "    case LayoutConstant::kVerticalTabStripHorizontalPadding:\n"
            "      return 12;\n"
        )
        new = (
            "    case LayoutConstant::kVerticalTabStripHorizontalPadding:\n"
            "      // XPLORER: favorites density. Less side chrome.\n"
            "      return 8;\n"
        )
        if old not in ctext:
            sys.exit(f"ANCHOR NOT FOUND in {constants} (horizontal padding)")
        constants.write_text(ctext.replace(old, new, 1))
        print(f"  edited: {constants}")

    style = src / "chrome/browser/ui/views/tabs/vertical_tab_style_views.cc"
    stext = style.read_text()
    if "XPLORER: favorites density" in stext:
        print(f"  skip (already applied): {style}")
    else:
        old = (
            "  // XPLORER: Arc soft pill — float the fill inside the row so tabs don't\n"
            "  // read as stacked Chrome cards. Pinned squares stay full-bleed.\n"
            "  if (!delegate_->IsPinned()) {\n"
        )
        new = (
            "  // XPLORER: Arc soft pill — float the fill inside the row so tabs don't\n"
            "  // read as stacked Chrome cards. Collapsed pinned squares stay\n"
            "  // full-bleed; a wide favorites row insets like the other pills.\n"
            "  // XPLORER: favorites density.\n"
            "  const bool wide_row = bounds.width() > bounds.height() * 1.5f;\n"
            "  if (!delegate_->IsPinned() || wide_row) {\n"
        )
        if old not in stext:
            sys.exit(f"ANCHOR NOT FOUND in {style} (pill inset)")
        stext = stext.replace(old, new, 1)
        old = (
            "gfx::Insets VerticalTabStyleViews::GetContentsInsets() const {\n"
            "  return gfx::Insets::VH(\n"
            "      GetLayoutConstant(LayoutConstant::kTabVerticalPadding),\n"
            "      GetLayoutConstant(LayoutConstant::kTabHorizontalPadding));\n"
            "}\n"
        )
        new = (
            "gfx::Insets VerticalTabStyleViews::GetContentsInsets() const {\n"
            "  // XPLORER: favorites density. Shared tab insets are 6px, sized\n"
            "  // for horizontal tabs. A 26px sidebar row only needs a hair.\n"
            "  return gfx::Insets::VH(\n"
            "      2,\n"
            "      GetLayoutConstant(LayoutConstant::kTabHorizontalPadding));\n"
            "}\n"
        )
        if old not in stext:
            sys.exit(f"ANCHOR NOT FOUND in {style} (contents insets)")
        style.write_text(stext.replace(old, new, 1))
        print(f"  edited: {style}")

    pinned = src / "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.cc"
    ptext = pinned.read_text()
    if "XPLORER: favorites density" in ptext:
        print(f"  skip (already applied): {pinned}")
    else:
        old = (
            "    auto collapse_state = GetTabStripCollapseState();\n"
            "\n"
            "    // Apply horizontal padding immediately at start of collapse animation by\n"
            "    // including collapsing state.\n"
            "    int available_width =\n"
            "        size_bounds.width().value() -\n"
            "        GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);\n"
            "\n"
            "    // When we are in collapsed state, only one child should be shown per row.\n"
            "    // During collapse animation and other cases, fit as many as possible.\n"
            "    children_on_row =\n"
            "        tabs::IsVerticalTabsExpandOnHoverFeatureEnabled() &&\n"
            "                collapse_state ==\n"
            "                    tabs::VerticalTabStripCollapseState::kCollapsed\n"
            "            ? 1\n"
            "            : std::min(\n"
            "                  children_on_row,\n"
            "                  static_cast<int>(std::floor((available_width - child_width) /\n"
            "                                              (child_width + kTabPadding)) +\n"
            "                                   1));\n"
        )
        new = (
            "    // XPLORER: favorites density. One full-width row per pinned tab,\n"
            "    // not a wrapping grid of icon tiles. The collapsed rail is\n"
            "    // already one column because its width is the rail width.\n"
            "    int available_width =\n"
            "        size_bounds.width().value() -\n"
            "        GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);\n"
            "\n"
            "    children_on_row = 1;\n"
        )
        if old not in ptext:
            sys.exit(f"ANCHOR NOT FOUND in {pinned} (column)")
        ptext = ptext.replace(old, new, 1)
        old = "        y = total_height + kTabPadding;\n"
        new = (
            "        // XPLORER: favorites density. 2px between rows, not 4.\n"
            "        y = total_height + 2;\n"
        )
        if old not in ptext:
            sys.exit(f"ANCHOR NOT FOUND in {pinned} (row gap)")
        pinned.write_text(ptext.replace(old, new, 1))
        print(f"  edited: {pinned}")

    layout = src / "chrome/browser/ui/views/tabs/common/tab_view_vertical_layout.cc"
    ltext = layout.read_text()
    if "XPLORER: favorites density" in ltext:
        print(f"  skip (already applied): {layout}")
    else:
        old = (
            "  const bool is_centered = (TabView().pinned_ || TabView().collapsed_) &&\n"
            "                           !TabView().IsInExpandOnHover(width);\n"
        )
        new = (
            "  // XPLORER: favorites density. An open pinned row keeps the icon\n"
            "  // leading so the title can sit beside it. The collapsed rail\n"
            "  // still centers the icon.\n"
            "  const bool is_centered =\n"
            "      TabView().collapsed_ && !TabView().IsInExpandOnHover(width);\n"
        )
        if old not in ltext:
            sys.exit(f"ANCHOR NOT FOUND in {layout} (centered)")
        ltext = ltext.replace(old, new, 1)
        old = (
            "  if (child_view == TabView().title_) {\n"
            "    // Pinned titles should be visible in the expand on hover state when the\n"
            "    // width is sufficient to show the title.\n"
            "    return !TabView().pinned_ || TabView().IsInExpandOnHover(width);\n"
            "  }\n"
        )
        new = (
            "  if (child_view == TabView().title_) {\n"
            "    // XPLORER: favorites density. Pinned titles show when the\n"
            "    // sidebar is open, not only during expand-on-hover.\n"
            "    if (TabView().pinned_) {\n"
            "      return !TabView().collapsed_ || TabView().IsInExpandOnHover(width);\n"
            "    }\n"
            "    return true;\n"
            "  }\n"
        )
        if old not in ltext:
            sys.exit(f"ANCHOR NOT FOUND in {layout} (title)")
        layout.write_text(ltext.replace(old, new, 1))
        print(f"  edited: {layout}")


def patch_quiet_sidebar_toolbar(src: Path):
    """Arc/Dia: no tab-search cluster or hairline above the space header."""
    top = src / (
        "chrome/browser/ui/views/tabs/vertical/"
        "vertical_tab_strip_top_container.cc"
    )
    text = top.read_text()
    if "XPLORER: no sidebar tab-search toolbar" in text:
        print(f"  skip (already applied): {top}")
    else:
        def swap(old: str, new: str, label: str):
            nonlocal text
            if old not in text:
                sys.exit(f"ANCHOR NOT FOUND in {top} ({label})")
            text = text.replace(old, new, 1)

        swap(
            "  combo_button_->SetOrientation(\n"
            "      combo_button_orientation_ = state_controller->IsCollapsed()\n"
            "                                      ? views::LayoutOrientation::kVertical\n"
            "                                      : views::LayoutOrientation::kHorizontal);\n"
            "}\n",
            "  combo_button_->SetOrientation(\n"
            "      combo_button_orientation_ = state_controller->IsCollapsed()\n"
            "                                      ? views::LayoutOrientation::kVertical\n"
            "                                      : views::LayoutOrientation::kHorizontal);\n"
            "  // XPLORER: no sidebar tab-search toolbar. Arc's space header is\n"
            "  // the top of the list; tab search stays on the keyboard.\n"
            "  combo_button_->SetVisible(false);\n"
            "  if (combo_button_->start_button()) {\n"
            "    combo_button_->start_button()->SetVisible(false);\n"
            "  }\n"
            "  if (combo_button_->end_button()) {\n"
            "    combo_button_->end_button()->SetVisible(false);\n"
            "  }\n"
            "}\n",
            "constructor hide",
        )
        swap(
            "void VerticalTabStripTopContainer::Layout(PassKey) {\n"
            "  LayoutSuperclass<views::View>(this);\n"
            "  combo_button_->SetOrientation(combo_button_orientation_);\n"
            "}\n",
            "void VerticalTabStripTopContainer::Layout(PassKey) {\n"
            "  // XPLORER: no sidebar tab-search toolbar. The action system\n"
            "  // turns these buttons back on, so hide them on every layout.\n"
            "  if (combo_button_) {\n"
            "    combo_button_->SetVisible(false);\n"
            "    if (combo_button_->start_button()) {\n"
            "      combo_button_->start_button()->SetVisible(false);\n"
            "    }\n"
            "    if (combo_button_->end_button()) {\n"
            "      combo_button_->end_button()->SetVisible(false);\n"
            "    }\n"
            "  }\n"
            "  LayoutSuperclass<views::View>(this);\n"
            "  combo_button_->SetOrientation(combo_button_orientation_);\n"
            "}\n",
            "Layout hide",
        )
        swap(
            "      if (start_button_visible || end_button_visible) {\n",
            "      if (combo_button_->GetVisible() &&\n"
            "          (start_button_visible || end_button_visible)) {\n",
            "collapsed layout",
        )
        swap(
            "    if (combo_button_) {\n"
            "      const gfx::Size pref_size = combo_button_->GetPreferredSizeForOrientation(\n"
            "          combo_button_orientation_);\n"
            "      right_alignment -= pref_size.width();\n",
            "    if (combo_button_ && combo_button_->GetVisible()) {\n"
            "      const gfx::Size pref_size = combo_button_->GetPreferredSizeForOrientation(\n"
            "          combo_button_orientation_);\n"
            "      right_alignment -= pref_size.width();\n",
            "expanded layout",
        )
        swap(
            "  // Combo Button\n"
            "  total_width += combo_button_\n"
            "                     ->GetPreferredSizeForOrientation(\n"
            "                         views::LayoutOrientation::kHorizontal)\n"
            "                     .width();\n",
            "  // Combo Button\n"
            "  // XPLORER: a hidden tab-search cluster must not widen the row.\n"
            "  if (combo_button_->GetVisible()) {\n"
            "    total_width += combo_button_\n"
            "                       ->GetPreferredSizeForOrientation(\n"
            "                           views::LayoutOrientation::kHorizontal)\n"
            "                       .width();\n"
            "  }\n",
            "preferred width",
        )
        swap(
            "  if (combo_button_) {\n"
            "    min_height =\n"
            "        std::max(min_height, combo_button_\n"
            "                                 ->GetPreferredSizeForOrientation(\n"
            "                                     views::LayoutOrientation::kHorizontal)\n"
            "                                 .height());\n"
            "  }\n",
            "  if (combo_button_ && combo_button_->GetVisible()) {\n"
            "    min_height =\n"
            "        std::max(min_height, combo_button_\n"
            "                                 ->GetPreferredSizeForOrientation(\n"
            "                                     views::LayoutOrientation::kHorizontal)\n"
            "                                 .height());\n"
            "  }\n",
            "baseline height",
        )
        top.write_text(text)
        print(f"  edited: {top}")

    region = src / "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc"
    rtext = region.read_text()
    if "XPLORER: no sidebar toolbar rule" in rtext:
        print(f"  skip (already applied): {region}")
        return
    old = (
        "  top_button_separator_->SetProperty(\n"
        "      views::kMarginsKey, gfx::Insets::VH(0, region_horizontal_padding));\n"
    )
    new = (
        "  top_button_separator_->SetProperty(\n"
        "      views::kMarginsKey, gfx::Insets::VH(0, region_horizontal_padding));\n"
        "  // XPLORER: no sidebar toolbar rule. The hairline under tab search\n"
        "  // made the space header look like a Chrome toolbar section.\n"
        "  top_button_separator_->SetVisible(false);\n"
    )
    if old not in rtext:
        sys.exit(f"ANCHOR NOT FOUND in {region} (toolbar rule)")
    region.write_text(rtext.replace(old, new, 1))
    print(f"  edited: {region}")


def patch_collapse_sidebar(src: Path):
    """Collapsed sidebar is zero width so only the page shows. Hovering the
    window's left edge reveals it. Traffic lights hide while collapsed.
    """
    frame = src / "chrome/browser/ui/views/frame/browser_frame_view_mac.mm"
    if "XplorerSetCaptionButtonsVisible" not in frame.read_text():
        edit(
            frame,
            '#include "ui/views/widget/widget.h"\n\nnamespace {',
            '#include "ui/views/widget/widget.h"\n\n'
            "#import <AppKit/AppKit.h>\n\n"
            "void XplorerSetCaptionButtonsVisible(gfx::NativeWindow window, "
            "bool visible) {\n"
            "  NSWindow* ns_window = window.GetNativeNSWindow();\n"
            "  if (!ns_window)\n"
            "    return;\n"
            "  const NSWindowButton buttons[] = {\n"
            "      NSWindowCloseButton, NSWindowMiniaturizeButton, "
            "NSWindowZoomButton};\n"
            "  for (NSWindowButton button_id : buttons) {\n"
            "    if (NSButton* button = [ns_window standardWindowButton:button_id])\n"
            "      button.hidden = !visible;\n"
            "  }\n"
            "}\n\n"
            "namespace {",
        )

    vts_h = src / "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
    htext = vts_h.read_text()
    if "kCollapsedWidth = 0" not in htext:
        edit(
            vts_h,
            "  static constexpr int kCollapsedWidth = 56;\n"
            "  // TODO(crbug.com/465833741): Determine snapping behavior.",
            "  // XPLORER: collapsed sidebar is the page only. Hover the left "
            "edge to show it.\n"
            "  static constexpr int kCollapsedWidth = 0;\n"
            "  // TODO(crbug.com/465833741): Determine snapping behavior.",
        )
    if "class EdgeHoverHandler" not in vts_h.read_text():
        edit(
            vts_h,
            "  class ClickEventHandler : public ui::EventHandler {",
            "  // XPLORER: a collapsed strip has no width, so hover the window's "
            "left edge.\n"
            "  class EdgeHoverHandler : public ui::EventHandler {\n"
            "   public:\n"
            "    explicit EdgeHoverHandler(VerticalTabStripRegionView* region_view)\n"
            "        : region_view_(region_view) {}\n"
            "    void OnMouseEvent(ui::MouseEvent* event) override;\n"
            "\n"
            "   private:\n"
            "    raw_ptr<VerticalTabStripRegionView> region_view_;\n"
            "  };\n"
            "\n",
            before=True,
        )
    if "edge_hover_handler_" not in vts_h.read_text():
        edit(
            vts_h,
            "  ClickEventHandler click_handler_{this};",
            "  EdgeHoverHandler edge_hover_handler_{this};\n",
            before=True,
        )

    vts_cc = src / "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc"
    if "XplorerSetCaptionButtonsVisible" not in vts_cc.read_text():
        edit(
            vts_cc,
            '#include "ui/display/screen.h"\n#include "ui/events/event.h"',
            '#include "ui/display/screen.h"\n'
            '#include "ui/gfx/native_ui_types.h"\n'
            "\n"
            "#if BUILDFLAG(IS_MAC)\n"
            "// Defined in browser_frame_view_mac.mm. Hides the traffic lights when the\n"
            "// sidebar is collapsed so only the page remains.\n"
            "extern void XplorerSetCaptionButtonsVisible(gfx::NativeWindow window,\n"
            "                                            bool visible);\n"
            "#endif\n"
            '#include "ui/events/event.h"',
        )
        edit(
            vts_cc,
            "void VerticalTabStripRegionView::AddedToWidget() {",
            "void VerticalTabStripRegionView::EdgeHoverHandler::OnMouseEvent(\n"
            "    ui::MouseEvent* event) {\n"
            "  if (!region_view_ || !region_view_->state_controller_ ||\n"
            "      !region_view_->state_controller_->IsCollapsed())\n"
            "    return;\n"
            "  if (event->type() != ui::EventType::kMouseMoved &&\n"
            "      event->type() != ui::EventType::kMouseEntered)\n"
            "    return;\n"
            "  views::Widget* widget = region_view_->GetWidget();\n"
            "  if (!widget)\n"
            "    return;\n"
            "  const gfx::Point cursor = display::Screen::Get()->GetCursorScreenPoint();\n"
            "  const gfx::Rect window = widget->GetWindowBoundsInScreen();\n"
            "  if (cursor.x() >= window.x() && cursor.x() <= window.x() + 14 &&\n"
            "      cursor.y() >= window.y() && cursor.y() <= window.bottom()) {\n"
            "    region_view_->UpdateExpandOnHoverState(true);\n"
            "  }\n"
            "}\n"
            "\n"
            "void VerticalTabStripRegionView::AddedToWidget() {",
            before=True,
        )
        edit(
            vts_cc,
            "void VerticalTabStripRegionView::AddedToWidget() {\n"
            "  BaseTabStripRegionView::AddedToWidget();\n"
            "  paint_as_active_subscription_ =",
            "void VerticalTabStripRegionView::AddedToWidget() {\n"
            "  BaseTabStripRegionView::AddedToWidget();\n"
            "  if (views::View* root = GetWidget()->GetRootView())\n"
            "    root->AddPreTargetHandler(&edge_hover_handler_);\n"
            "  paint_as_active_subscription_ =",
        )
        edit(
            vts_cc,
            "void VerticalTabStripRegionView::RemovedFromWidget() {\n"
            "  if (GetFocusManager()) {",
            "void VerticalTabStripRegionView::RemovedFromWidget() {\n"
            "  if (GetWidget()) {\n"
            "    if (views::View* root = GetWidget()->GetRootView())\n"
            "      root->RemovePreTargetHandler(&edge_hover_handler_);\n"
            "  }\n"
            "  if (GetFocusManager()) {",
        )
        edit(
            vts_cc,
            "  if (state == tabs::VerticalTabStripCollapseState::kExpanded ||\n"
            "      state == tabs::VerticalTabStripCollapseState::kCollapsed) {\n"
            "    UpdateExpandOnHoverState();\n"
            "  }\n"
            "}",
            "  if (state == tabs::VerticalTabStripCollapseState::kExpanded ||\n"
            "      state == tabs::VerticalTabStripCollapseState::kCollapsed) {\n"
            "    UpdateExpandOnHoverState();\n"
            "  }\n"
            "#if BUILDFLAG(IS_MAC)\n"
            "  if (GetWidget()) {\n"
            "    XplorerSetCaptionButtonsVisible(GetWidget()->GetNativeWindow(),\n"
            "                                    !collapsed || is_expanded_on_hover_);\n"
            "  }\n"
            "#endif\n"
            "}",
        )
        edit(
            vts_cc,
            "  BrowserAnimationController::From(browser_view()->browser())\n"
            "      ->Start(TabStripAnimations::kVerticalTabStrip,\n"
            "              expand ? TabStripAnimations::kExpandOnHover\n"
            "                     : TabStripAnimations::kCollapseOnHover);\n"
            "}",
            "  BrowserAnimationController::From(browser_view()->browser())\n"
            "      ->Start(TabStripAnimations::kVerticalTabStrip,\n"
            "              expand ? TabStripAnimations::kExpandOnHover\n"
            "                     : TabStripAnimations::kCollapseOnHover);\n"
            "#if BUILDFLAG(IS_MAC)\n"
            "  if (GetWidget()) {\n"
            "    XplorerSetCaptionButtonsVisible(\n"
            "        GetWidget()->GetNativeWindow(),\n"
            "        expand || !state_controller_->IsCollapsed());\n"
            "  }\n"
            "#endif\n"
            "}",
        )


def patch_sidebar_space_menu(src: Path):
    """Arc: the space menu is the sidebar's menu, not only the space-name row."""
    region = src / "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc"
    text = region.read_text()
    if "space menu belongs to the whole sidebar" in text:
        return
    edit(
        region,
        "  xplorer_sidebar_chrome_->SetProperty(\n"
        "      views::kFlexBehaviorKey,\n"
        "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
        "                               views::MaximumFlexSizeRule::kPreferred));\n"
        "}",
        "  xplorer_sidebar_chrome_->SetProperty(\n"
        "      views::kFlexBehaviorKey,\n"
        "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
        "                               views::MaximumFlexSizeRule::kPreferred));\n"
        "  // XPLORER: Arc's space menu belongs to the whole sidebar. Tabs and folder\n"
        "  // headers keep their own menus because those views install a controller.\n"
        "  set_context_menu_controller(xplorer_sidebar_chrome_);\n"
        "}",
    )


def patch_folder_rows(src: Path):
    """Arc folders: no colored group rail, folder icon on the header."""
    layout = src / "chrome/browser/ui/views/tabs/common/tab_group_view_layout.cc"
    text = layout.read_text()
    if "XPLORER: folders are icon" not in text:
        edit(
            layout,
            "  const bool show_group_line =\n"
            "      !tab_group_view->IsGroupFocused() && !tab_group_view->is_collapsed();",
            "  // XPLORER: folders are icon + name. The colored group rail is the\n"
            "  // orange bar beside Bookmarks.\n"
            "  const bool show_group_line = false;",
        )
    header = src / "chrome/browser/ui/views/tabs/common/tab_group_header_view.cc"
    htext = header.read_text()
    if "a vertical group is a folder row" not in htext:
        edit(
            header,
            "    sync_icon_->SetVisible(is_shared_);\n"
            "    if (is_shared_) {\n"
            "      sync_icon_->SetImage(ui::ImageModel::FromVectorIcon(\n"
            "          features::IsRoundedIconsEnabled() ? kGroupCustomIcon\n"
            "                                            : kPeopleGroupOldIcon,\n"
            "          foreground_color, kIconSize));\n"
            "    }",
            "    // XPLORER: a vertical group is a folder row (icon + name).\n"
            "    if (orientation_ == TabStripOrientation::kVertical && !is_shared_) {\n"
            "      sync_icon_->SetVisible(true);\n"
            "      sync_icon_->SetImage(ui::ImageModel::FromVectorIcon(\n"
            "          vector_icons::kFolderOpenIcon, foreground_color, kIconSize));\n"
            "    } else {\n"
            "      sync_icon_->SetVisible(is_shared_);\n"
            "      if (is_shared_) {\n"
            "        sync_icon_->SetImage(ui::ImageModel::FromVectorIcon(\n"
            "            features::IsRoundedIconsEnabled() ? kGroupCustomIcon\n"
            "                                              : kPeopleGroupOldIcon,\n"
            "            foreground_color, kIconSize));\n"
            "      }\n"
            "    }",
        )


def patch_vertical_sidebar(src: Path):
    """Arc-style sidebar chrome in the vertical tab strip.

    Injects XplorerSidebarChromeView (the "Tabs" section label) at the top of
    VerticalTabStripRegionView and auto-groups agent-owned tabs.
    """
    vts_h = src / "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
    edit(
        vts_h,
        "class VerticalTabStripBottomContainer;",
        "class VerticalTabStripBottomContainer;\n"
        "namespace xplorer {\n"
        "class XplorerSidebarChromeView;\n"
        "}  // namespace xplorer  // XPLORER",
    )
    # Scheduled-section forward declaration in its OWN namespace block, spliced
    # after the chrome view's block. Important: do NOT add a line *inside* the
    # chrome view's block — that block is the verbatim insertion of the edit
    # above, and mutating it would break that edit's idempotency guard and cause
    # it to re-fire (duplicating the block) on the next apply.
    if "XplorerSidebarScheduledView" not in vts_h.read_text():
        edit(
            vts_h,
            "class XplorerSidebarChromeView;\n"
            "}  // namespace xplorer  // XPLORER",
            "class XplorerSidebarChromeView;\n"
            "}  // namespace xplorer  // XPLORER\n"
            "namespace xplorer {\n"
            "class XplorerSidebarScheduledView;\n"
            "}  // namespace xplorer  // XPLORER",
        )
    vts_h_text = vts_h.read_text()
    if "InstallXplorerSidebarChrome" not in vts_h_text:
        edit(
            vts_h,
            "  VerticalTabStripTopContainer* GetTopContainer() {\n"
            "    return top_button_container_;\n"
            "  }",
            "  // XPLORER: Arc-style sidebar chrome below the top menu bar.\n"
            "  void InstallXplorerSidebarChrome(\n"
            "      std::unique_ptr<xplorer::XplorerSidebarChromeView> chrome);\n"
            "  xplorer::XplorerSidebarChromeView* xplorer_sidebar_chrome() {\n"
            "    return xplorer_sidebar_chrome_;\n"
            "  }\n\n"
            "  VerticalTabStripTopContainer* GetTopContainer() {\n"
            "    return top_button_container_;\n"
            "  }",
        )
    # Restate the anchor as the insertion's FIRST line. edit() only replaces
    # when the first or last line matches, and anchor.strip() drops the indent
    # of a one-line anchor, so a last-line match silently fails and the new
    # member gets glued onto the same line.
    edit(
        vts_h,
        "  raw_ptr<VerticalTabStripTopContainer> top_button_container_ = nullptr;",
        "  raw_ptr<VerticalTabStripTopContainer> top_button_container_ = nullptr;\n"
        "  raw_ptr<xplorer::XplorerSidebarChromeView> xplorer_sidebar_chrome_ =\n"
        "      nullptr;  // XPLORER\n"
        "  raw_ptr<xplorer::XplorerSidebarScheduledView> xplorer_sidebar_scheduled_ =\n"
        "      nullptr;  // XPLORER",
    )
    # Scheduled-section installer + accessor, mirroring the chrome view.
    if "InstallXplorerSidebarScheduled" not in vts_h.read_text():
        edit(
            vts_h,
            "  void InstallXplorerSidebarChrome(\n"
            "      std::unique_ptr<xplorer::XplorerSidebarChromeView> chrome);\n"
            "  xplorer::XplorerSidebarChromeView* xplorer_sidebar_chrome() {\n"
            "    return xplorer_sidebar_chrome_;\n"
            "  }",
            "  void InstallXplorerSidebarChrome(\n"
            "      std::unique_ptr<xplorer::XplorerSidebarChromeView> chrome);\n"
            "  xplorer::XplorerSidebarChromeView* xplorer_sidebar_chrome() {\n"
            "    return xplorer_sidebar_chrome_;\n"
            "  }\n"
            "  // XPLORER: native \"Scheduled\" section, BELOW the tab list.\n"
            "  void InstallXplorerSidebarScheduled(\n"
            "      std::unique_ptr<xplorer::XplorerSidebarScheduledView> scheduled);\n"
            "  xplorer::XplorerSidebarScheduledView* xplorer_sidebar_scheduled() {\n"
            "    return xplorer_sidebar_scheduled_;\n"
            "  }",
        )
    # Scheduled-section member, appended AFTER the chrome view member's full
    # block. The guard checks the member-declaration text specifically (the
    # accessor above already put the bare name "xplorer_sidebar_scheduled_" into
    # the file). We RESTATE the chrome member block verbatim and add the
    # scheduled member after its last line, so the chrome member edit's exact
    # insertion stays present contiguously and its idempotency guard keeps
    # passing (splitting that block would make it re-fire and duplicate).
    if ("raw_ptr<xplorer::XplorerSidebarScheduledView> xplorer_sidebar_scheduled_"
            not in vts_h.read_text()):
        edit(
            vts_h,
            "  raw_ptr<xplorer::XplorerSidebarChromeView> xplorer_sidebar_chrome_ =\n"
            "      nullptr;  // XPLORER\n"
            "  raw_ptr<VerticalTabStripTopContainer> top_button_container_ = nullptr;",
            "  raw_ptr<xplorer::XplorerSidebarChromeView> xplorer_sidebar_chrome_ =\n"
            "      nullptr;  // XPLORER\n"
            "  raw_ptr<VerticalTabStripTopContainer> top_button_container_ = nullptr;\n"
            "  raw_ptr<xplorer::XplorerSidebarScheduledView> xplorer_sidebar_scheduled_ =\n"
            "      nullptr;  // XPLORER",
        )

    vts_cc = src / "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc"
    edit(
        vts_cc,
        '#include "chrome/browser/ui/views/frame/browser_view.h"',
        '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
        '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"'
        "  // XPLORER",
    )
    if "xplorer_sidebar_scheduled_view.h" not in vts_cc.read_text():
        edit(
            vts_cc,
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"'
            "  // XPLORER",
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"'
            "  // XPLORER\n"
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_scheduled_view.h"'
            "  // XPLORER",
        )
    vts_cc_text = vts_cc.read_text()
    if "InstallXplorerSidebarChrome" not in vts_cc_text:
        edit(
            vts_cc,
            "VerticalTabStripRegionView::~VerticalTabStripRegionView() {",
            "void VerticalTabStripRegionView::InstallXplorerSidebarChrome(\n"
            "    std::unique_ptr<xplorer::XplorerSidebarChromeView> chrome) {\n"
            "  // Below the collapse/tab-search top bar, not above it.\n"
            "  size_t insert_index = 0;\n"
            "  if (top_button_separator_) {\n"
            "    insert_index = GetIndexOf(top_button_separator_).value() + 1;\n"
            "  } else if (top_button_container_) {\n"
            "    insert_index = GetIndexOf(top_button_container_).value() + 1;\n"
            "  }\n"
            "  xplorer_sidebar_chrome_ = AddChildViewAt(std::move(chrome), insert_index);\n"
            "  const int region_horizontal_padding =\n"
            "      GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);\n"
            "  xplorer_sidebar_chrome_->SetProperty(\n"
            "      views::kMarginsKey, gfx::Insets::VH(0, region_horizontal_padding));\n"
            "  xplorer_sidebar_chrome_->SetProperty(\n"
            "      views::kFlexBehaviorKey,\n"
            "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
            "                               views::MaximumFlexSizeRule::kPreferred));\n"
            "}\n\n"
            "VerticalTabStripRegionView::~VerticalTabStripRegionView() {",
        )
    if "tab_strip_index" not in vts_cc.read_text():
        edit(
            vts_cc,
            "  std::optional<size_t> separator_index = GetIndexOf(top_button_separator_);\n"
            "  CHECK(separator_index.has_value());\n"
            "  ReorderChildView(tab_strip_view(), separator_index.value() + 1);",
            "  std::optional<size_t> separator_index = GetIndexOf(top_button_separator_);\n"
            "  CHECK(separator_index.has_value());\n"
            "  size_t tab_strip_index = separator_index.value() + 1;\n"
            "  if (xplorer_sidebar_chrome_) {\n"
            "    tab_strip_index = GetIndexOf(xplorer_sidebar_chrome_).value() + 1;\n"
            "  }\n"
            "  ReorderChildView(tab_strip_view(), tab_strip_index);  // XPLORER",
        )
    # InstallXplorerSidebarScheduled: append the "Scheduled" section below the tab
    # list. At install time tab_strip_view_ does not exist yet (the tab strip is
    # created lazily via InitializeTabStrip()/SetTabStripView, well after this
    # runs), so just append it for now; SetTabStripView re-anchors it directly
    # after tab_strip_view_ (see the reorder below). That ordering puts it between
    # the scrollable tab list and the bottom new-tab container.
    if "InstallXplorerSidebarScheduled" not in vts_cc.read_text():
        edit(
            vts_cc,
            "VerticalTabStripRegionView::~VerticalTabStripRegionView() {",
            "void VerticalTabStripRegionView::InstallXplorerSidebarScheduled(\n"
            "    std::unique_ptr<xplorer::XplorerSidebarScheduledView> scheduled) {\n"
            "  // Append for now; SetTabStripView() reorders it to sit just below\n"
            "  // tab_strip_view_ once the tab strip exists.\n"
            "  xplorer_sidebar_scheduled_ = AddChildView(std::move(scheduled));\n"
            "  const int region_horizontal_padding =\n"
            "      GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);\n"
            "  xplorer_sidebar_scheduled_->SetProperty(\n"
            "      views::kMarginsKey, gfx::Insets::VH(0, region_horizontal_padding));\n"
            "  xplorer_sidebar_scheduled_->SetProperty(\n"
            "      views::kFlexBehaviorKey,\n"
            "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
            "                               views::MaximumFlexSizeRule::kPreferred));\n"
            "  if (tab_strip_view()) {\n"
            "    ReorderChildView(xplorer_sidebar_scheduled_,\n"
            "                     GetIndexOf(tab_strip_view()).value() + 1);\n"
            "  }\n"
            "}\n\n"
            "VerticalTabStripRegionView::~VerticalTabStripRegionView() {",
        )
    # Keep the Scheduled section directly below the tab list whenever the tab
    # strip view is (re)installed. Anchor on the SetTabStripView() tail (the
    # collapse-state call + return) so this is independent of whether the
    # tab_strip_index reorder line carries a "// XPLORER" suffix (it does on a
    # fresh apply, but may not on a tree patched by an earlier script).
    if "if (xplorer_sidebar_scheduled_) {  // XPLORER" not in vts_cc.read_text():
        edit(
            vts_cc,
            "  ReorderChildView(tab_strip_view(), tab_strip_index);  // XPLORER\n\n"
            "  OnCollapseStateChanged(state_controller_->GetCollapseState());\n"
            "}",
            "  ReorderChildView(tab_strip_view(), tab_strip_index);  // XPLORER\n\n"
            "  if (xplorer_sidebar_scheduled_) {  // XPLORER\n"
            "    ReorderChildView(xplorer_sidebar_scheduled_,\n"
            "                     GetIndexOf(tab_strip_view()).value() + 1);\n"
            "  }\n\n"
            "  OnCollapseStateChanged(state_controller_->GetCollapseState());\n"
            "}",
        )

    browser_view_h = src / "chrome/browser/ui/views/frame/browser_view.h"
    edit(
        browser_view_h,
        "class BookmarkBarView;",
        "class BookmarkBarView;\n"
        "namespace xplorer {\n"
        "class AgentTabGrouper;\n"
        "class XplorerSidebarChromeView;\n"
        "}  // namespace xplorer  // XPLORER",
    )
    edit(
        browser_view_h,
        "  TopContainerView* top_container() { return top_container_; }",
        "  TopContainerView* top_container() { return top_container_; }\n\n"
        "  // XPLORER: Arc-style vertical sidebar chrome accessors.\n"
        "  VerticalTabStripRegionView* vertical_tab_strip_region_view() {\n"
        "    return vertical_tab_strip_region_view_;\n"
        "  }\n"
        "  xplorer::XplorerSidebarChromeView* xplorer_sidebar_chrome() {\n"
        "    return xplorer_sidebar_chrome_;\n"
        "  }",
    )
    edit(
        browser_view_h,
        "  raw_ptr<BookmarkBarView> bookmark_bar_view_ = nullptr;",
        "  raw_ptr<BookmarkBarView> bookmark_bar_view_ = nullptr;\n"
        "  raw_ptr<xplorer::XplorerSidebarChromeView> xplorer_sidebar_chrome_ =\n"
        "      nullptr;  // XPLORER\n"
        "  std::unique_ptr<xplorer::AgentTabGrouper> agent_tab_grouper_;  // XPLORER",
    )

    browser_view_cc = src / "chrome/browser/ui/views/frame/browser_view.cc"
    edit(
        browser_view_cc,
        '#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"',
        '#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"\n'
        '#include "chrome/browser/ui/views/xplorer/xplorer_agent_tab_grouper.h"  // XPLORER\n'
        '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"  // XPLORER',
    )
    # Append the scheduled-view include AFTER the chrome include block's last
    # line (restated), not inside it: the chrome include edit above has no outer
    # name-guard and relies on edit()'s verbatim-presence check, so splitting its
    # block would duplicate the whole block on the next apply.
    if "xplorer_sidebar_scheduled_view.h" not in browser_view_cc.read_text():
        edit(
            browser_view_cc,
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"  // XPLORER',
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"  // XPLORER\n'
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_scheduled_view.h"  // XPLORER',
        )
    edit(
        browser_view_cc,
        "    vertical_tab_strip_region_view_ =\n"
        "        AddChildView(std::move(vertical_tab_strip_container));",
        "    vertical_tab_strip_region_view_ =\n"
        "        AddChildView(std::move(vertical_tab_strip_container));\n\n"
        "    // XPLORER: Arc-style sidebar chrome (\"Tabs\" section label)\n"
        "    // + auto-group agent-owned tabs.\n"
        "    {\n"
        "      auto sidebar_chrome =\n"
        "          std::make_unique<xplorer::XplorerSidebarChromeView>(\n"
        "              browser_.get(), browser_->GetProfile());\n"
        "      xplorer_sidebar_chrome_ = sidebar_chrome.get();\n"
        "      vertical_tab_strip_region_view_->InstallXplorerSidebarChrome(\n"
        "          std::move(sidebar_chrome));\n"
        "      agent_tab_grouper_ = std::make_unique<xplorer::AgentTabGrouper>(\n"
        "          browser_->GetTabStripModel());\n"
        "    }",
    )
    # XPLORER: native "Scheduled" section, installed AFTER the chrome view so it
    # renders below the tab list (sidebar order: Bookmarks -> Tabs -> Scheduled).
    # Spliced AFTER the chrome instantiation block's closing brace (restated
    # verbatim) rather than inside it: the chrome instantiation edit above has no
    # outer name-guard and relies on edit()'s verbatim-presence check, so
    # mutating its block would make it re-fire and duplicate on the next apply.
    # InstallXplorerSidebarScheduled only needs the region view member, so a
    # standalone block after the chrome block is fine.
    if "InstallXplorerSidebarScheduled" not in browser_view_cc.read_text():
        edit(
            browser_view_cc,
            "      agent_tab_grouper_ = std::make_unique<xplorer::AgentTabGrouper>(\n"
            "          browser_->GetTabStripModel());\n"
            "    }",
            "      agent_tab_grouper_ = std::make_unique<xplorer::AgentTabGrouper>(\n"
            "          browser_->GetTabStripModel());\n"
            "    }\n"
            "    // XPLORER: native \"Scheduled\" section below the tab list.\n"
            "    vertical_tab_strip_region_view_->InstallXplorerSidebarScheduled(\n"
            "        std::make_unique<xplorer::XplorerSidebarScheduledView>(\n"
            "            browser_.get()));",
        )

    browser_ui_gn = src / "chrome/browser/ui/BUILD.gn"
    # Order matters: the chrome-view block below adds the
    # xplorer_agent_tab_grouper.h line, which the scheduled_task_tabs block
    # anchors on. On a clean chromium reset that anchor does not exist until
    # this block adds it, so the chrome-view block MUST run first. Each block
    # keeps its own verbatim-presence guard for idempotency.
    observer_cc = '      "views/xplorer/xplorer_bookmark_tab_observer.cc",  # XPLORER\n'
    observer_h = '      "views/xplorer/xplorer_bookmark_tab_observer.h",  # XPLORER\n'
    gn_text = browser_ui_gn.read_text()
    if observer_cc in gn_text or observer_h in gn_text:
        gn_text = gn_text.replace(observer_cc, "").replace(observer_h, "")
        browser_ui_gn.write_text(gn_text)
        print(f"  removed bookmark_tab_observer from: {browser_ui_gn}")

    if "xplorer_sidebar_chrome_view.cc" not in browser_ui_gn.read_text():
        edit(
            browser_ui_gn,
            '      "views/frame/vertical_tab_strip_region_view.cc",\n'
            '      "views/frame/vertical_tab_strip_region_view.h",',
            '      "views/frame/vertical_tab_strip_region_view.cc",\n'
            '      "views/frame/vertical_tab_strip_region_view.h",\n'
            '      "views/xplorer/xplorer_sidebar_chrome_view.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_chrome_view.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_row_button.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_row_button.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_section_label.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_section_label.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_agent_tab_grouper.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_agent_tab_grouper.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_settings_nav.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_settings_nav.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_scheduled_view.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_scheduled_view.h",  # XPLORER',
        )

    if "xplorer_scheduled_task_tabs.cc" not in browser_ui_gn.read_text():
        edit(
            browser_ui_gn,
            '      "views/xplorer/xplorer_agent_tab_grouper.h",  # XPLORER\n',
            '      "views/xplorer/xplorer_agent_tab_grouper.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_scheduled_task_tabs.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_scheduled_task_tabs.h",  # XPLORER\n',
        )

    # M153 deleted chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_view.h
    # (and the unpinned-container / group-header siblings). The bookmark-row
    # hiding and group-header chat button were patched into those classes.
    # Re-porting them onto views/tabs/common/* is a follow-up; skip so the
    # security rebuild still applies.
    if not (src / "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_view.h").exists():
        print("  skip (vertical tab strip views moved in M153): "
              "bookmark-row hiding and group-header chat button")
        return

    # XPLORER: Arc-style bookmark tabs hide their row from the vertical tab list.
    # Hiding is STATEFUL: a tab handle stays in `hidden_rows_` so it can be
    # re-asserted at the relayout boundary (OnAnimationEnded in the unpinned
    # container), mirroring how collapsed tab groups re-assert visibility. A
    # one-shot SetVisible(false) is otherwise clobbered when the insert
    # animation's TabCollectionAnimatingLayoutManager forces the new row back
    # visible for the duration of the animation.
    vts_view_h = (src / "chrome/browser/ui/views/tabs/vertical/"
                  "vertical_tab_strip_view.h")
    vts_view_h_text = vts_view_h.read_text()
    if "SetTabRowVisible" not in vts_view_h_text:
        # flat_set for the persistent set of hidden tab-row handles.
        edit(
            vts_view_h,
            '#include "base/memory/raw_ptr.h"',
            '#include "base/containers/flat_set.h"  // XPLORER\n'
            '#include "base/memory/raw_ptr.h"',
        )
        # Public API: SetTabRowVisible + ReassertHiddenRows.
        edit(
            vts_view_h,
            "  void OnTabChanged(const tabs::TabInterface* active_tab);\n\n"
            "  void RecordMousePressedInTab();",
            "  void OnTabChanged(const tabs::TabInterface* active_tab);\n\n"
            "  // XPLORER: hide/show a tab row (Arc-style sidebar bookmark tabs).\n"
            "  // Hiding is stateful so it survives insert + activate + the insert\n"
            "  // animation; ReassertHiddenRows() re-applies SetVisible(false) at\n"
            "  // the relayout boundary (the unpinned container's OnAnimationEnded).\n"
            "  void SetTabRowVisible(const tabs::TabHandle& handle, bool visible);\n"
            "  void ReassertHiddenRows();\n\n"
            "  void RecordMousePressedInTab();",
        )
        # Private member: the persistent set of hidden tab-row handles.
        edit(
            vts_view_h,
            "  bool is_collapsed_ = false;",
            "  bool is_collapsed_ = false;\n\n"
            "  // XPLORER: handles of tab rows hidden from the strip. Kept so the\n"
            "  // hidden state can be re-asserted after the insert animation, which\n"
            "  // would otherwise force a freshly-inserted row back to visible.\n"
            "  base::flat_set<tabs::TabHandle> hidden_rows_;",
        )
    vts_view_cc = (src / "chrome/browser/ui/views/tabs/vertical/"
                   "vertical_tab_strip_view.cc")
    if "VerticalTabStripView::SetTabRowVisible" not in vts_view_cc.read_text():
        edit(
            vts_view_cc,
            "BEGIN_METADATA(VerticalTabStripView)",
            "void VerticalTabStripView::SetTabRowVisible(\n"
            "    const tabs::TabHandle& handle,\n"
            "    bool visible) {\n"
            "  // Track the hidden state so it can be re-asserted after the insert\n"
            "  // animation (see ReassertHiddenRows / the unpinned container's\n"
            "  // OnAnimationEnded).\n"
            "  if (visible) {\n"
            "    hidden_rows_.erase(handle);\n"
            "  } else {\n"
            "    hidden_rows_.insert(handle);\n"
            "  }\n"
            "  if (!collection_node_) {\n"
            "    return;\n"
            "  }\n"
            "  TabCollectionNode* node = collection_node_->GetNodeForHandle(handle);\n"
            "  if (!node || !node->view()) {\n"
            "    return;\n"
            "  }\n"
            "  node->view()->SetVisible(visible);\n"
            "  InvalidateLayout();\n"
            "}\n\n"
            "void VerticalTabStripView::ReassertHiddenRows() {\n"
            "  if (hidden_rows_.empty() || !collection_node_) {\n"
            "    return;\n"
            "  }\n"
            "  // Re-apply SetVisible(false) for every still-hidden row, dropping\n"
            "  // handles whose node/view has gone away. The next\n"
            "  // CalculateProposedLayout then reads GetVisible()==false and the row\n"
            "  // stays hidden.\n"
            "  for (auto it = hidden_rows_.begin(); it != hidden_rows_.end();) {\n"
            "    TabCollectionNode* node = collection_node_->GetNodeForHandle(*it);\n"
            "    if (!node || !node->view()) {\n"
            "      it = hidden_rows_.erase(it);\n"
            "      continue;\n"
            "    }\n"
            "    node->view()->SetVisible(false);\n"
            "    // XPLORER: a regroup reparents the row into a VerticalTabGroupView\n"
            "    // and leaves a stale opacity layer from the move-fade; undo it so\n"
            "    // the row isn't a transparent ghost when re-shown by a later layout.\n"
            "    if (node->view()->layer()) {\n"
            "      node->view()->layer()->SetOpacity(1.0f);\n"
            "    }\n"
            "    ++it;\n"
            "  }\n"
            "}\n\n"
            "BEGIN_METADATA(VerticalTabStripView)",
        )

    # XPLORER: re-assert hidden bookmark-tab rows at the relayout boundary.
    # The unpinned container is a TabCollectionAnimatingLayoutManager::Delegate;
    # overriding OnAnimationEnded() lets us re-apply SetVisible(false) right
    # after the insert animation ends (mirrors VerticalTabGroupView, which
    # re-asserts collapse there). Access the strip via the existing
    # GetVerticalTabStripView() ancestry helper — no new member, no xplorer dep.
    vutc_view_h = (src / "chrome/browser/ui/views/tabs/vertical/"
                   "vertical_unpinned_tab_container_view.h")
    if "OnAnimationEnded" not in vutc_view_h.read_text():
        edit(
            vutc_view_h,
            "  bool ShouldAnimateOpacityForAddAndRemove(\n"
            "      const views::View& child_view) const override;",
            "  bool ShouldAnimateOpacityForAddAndRemove(\n"
            "      const views::View& child_view) const override;\n"
            "  // XPLORER: re-assert hidden bookmark-tab rows once the insert/move\n"
            "  // animation settles, so the row stays hidden from the strip.\n"
            "  void OnAnimationEnded() override;",
        )
    vutc_view_cc = (src / "chrome/browser/ui/views/tabs/vertical/"
                    "vertical_unpinned_tab_container_view.cc")
    vutc_cc_text = vutc_view_cc.read_text()
    if "VerticalUnpinnedTabContainerView::OnAnimationEnded" not in vutc_cc_text:
        edit(
            vutc_view_cc,
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_controller.h"',
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_controller.h"\n'
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_utils.h"  // XPLORER\n'
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_view.h"  // XPLORER',
        )
        edit(
            vutc_view_cc,
            "BEGIN_METADATA(VerticalUnpinnedTabContainerView)",
            "// XPLORER: re-apply the hidden state of bookmark-tab rows after the\n"
            "// animating layout manager finishes. The insert animation forces a\n"
            "// freshly-added row back to visible for its duration; re-asserting\n"
            "// here makes the hidden state durable (the next layout reads\n"
            "// GetVisible()==false and the row persists hidden).\n"
            "void VerticalUnpinnedTabContainerView::OnAnimationEnded() {\n"
            "  if (VerticalTabStripView* strip = GetVerticalTabStripView(this)) {\n"
            "    strip->ReassertHiddenRows();\n"
            "  }\n"
            "}\n\n"
            "BEGIN_METADATA(VerticalUnpinnedTabContainerView)",
        )

    # XPLORER: re-assert hidden rows at the GROUP-view relayout boundary too.
    # When the grouper regroups a hidden scheduled-task / bookmark row, the row is
    # reparented INTO a VerticalTabGroupView: DetachChildView force-shows it
    # (SetVisible(true)) and the reparent move-fade leaves a stale opacity layer.
    # The unpinned container's OnAnimationEnded never fires for that intra-group
    # move, so the row would otherwise stay half-visible with transparent title
    # text. Re-asserting from the group view's own OnAnimationEnded re-hides it.
    # The group-view .cc already includes vertical_tab_strip_utils.h (for
    # GetVerticalTabStripView) + vertical_tab_strip_view.h; add them only if a
    # future upstream drops them.
    vtg_view_h = (src / "chrome/browser/ui/views/tabs/vertical/"
                  "vertical_tab_group_view.h")
    if "void OnAnimationEnded() override;" not in vtg_view_h.read_text():
        sys.exit("ANCHOR NOT FOUND: VerticalTabGroupView::OnAnimationEnded "
                 "override decl missing — upstream moved; update apply_integration.py")
    vtg_view_cc = (src / "chrome/browser/ui/views/tabs/vertical/"
                   "vertical_tab_group_view.cc")
    vtg_cc_text = vtg_view_cc.read_text()
    if ('#include "chrome/browser/ui/views/tabs/vertical/'
            'vertical_tab_strip_utils.h"') not in vtg_cc_text:
        edit(
            vtg_view_cc,
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_controller.h"',
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_controller.h"\n'
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_utils.h"  // XPLORER',
        )
    if ('#include "chrome/browser/ui/views/tabs/vertical/'
            'vertical_tab_strip_view.h"') not in vtg_cc_text:
        edit(
            vtg_view_cc,
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_utils.h"',
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_utils.h"\n'
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_view.h"  // XPLORER',
        )
    if "strip->ReassertHiddenRows();" not in vtg_cc_text:
        edit(
            vtg_view_cc,
            "void VerticalTabGroupView::OnAnimationEnded() {\n"
            "  // For collapsed tab groups update child visibility only once animations have\n"
            "  // completed. This allows tabs to remain visible as the group animates closed.\n"
            "  if (tab_group_visual_data_.is_collapsed()) {\n"
            "    UpdateChildVisibilityForCollapseState(true);\n"
            "  }\n"
            "}",
            "void VerticalTabGroupView::OnAnimationEnded() {\n"
            "  // XPLORER: a scheduled-task / bookmark row reparented INTO this group is\n"
            "  // force-shown by DetachChildView and given an opacity layer by the reparent\n"
            "  // move-fade; re-assert its hidden state (the unpinned container's\n"
            "  // OnAnimationEnded never fires for an intra-group move).\n"
            "  if (VerticalTabStripView* strip = GetVerticalTabStripView(this)) {\n"
            "    strip->ReassertHiddenRows();\n"
            "  }\n"
            "  // For collapsed tab groups update child visibility only once animations have\n"
            "  // completed. This allows tabs to remain visible as the group animates closed.\n"
            "  if (tab_group_visual_data_.is_collapsed()) {\n"
            "    UpdateChildVisibilityForCollapseState(true);\n"
            "  }\n"
            "}",
        )

    # XPLORER: collapse the layout slot of a hidden tab row. CalculateProposedLayout
    # marks an invisible child as not-painted but still adds bounds.height() to the
    # running height, leaving a ~30px gap where an Arc bookmark tab's row was. Skip
    # invisible children entirely so the hidden row takes zero space.
    if "XPLORER: hidden rows take zero space" not in vutc_cc_text:
        edit(
            vutc_view_cc,
            "  for (auto* child : children) {\n"
            "    // The leading inset should not be applied for tab groups when the tab strip",
            "  for (auto* child : children) {\n"
            "    // XPLORER: hidden rows take zero space (Arc bookmark tabs are hidden\n"
            "    // from the strip; the sidebar bookmark row is the affordance).\n"
            "    if (!child->GetVisible()) {\n"
            "      layouts.child_layouts.emplace_back(child, false, gfx::Rect());\n"
            "      continue;\n"
            "    }\n"
            "    // The leading inset should not be applied for tab groups when the tab strip",
        )

    # XPLORER: "open chat" button on chat-owned tab-group headers. Each tab
    # group whose tabs are owned by a "chat:<conv_id>" agent gets a small button
    # on its header that opens the Grok side panel to that conversation. Mirrors
    # editor_bubble_button_'s pattern (a header button with a bound callback).
    # Patches PRISTINE upstream files; NO BUILD.gn dep is added (a grok_companion
    # dep here would create a GN cycle) — we rely on final-link symbol resolution,
    # the established fork pattern (cf. grok_native.cc including
    # grok_companion_util.h with no GN dep).
    vtgh_h = (src / "chrome/browser/ui/views/tabs/vertical/"
              "vertical_tab_group_header_view.h")
    # Forward-declare views::ImageButton alongside the other views fwd decls.
    edit(
        vtgh_h,
        "namespace views {\n"
        "class LabelButton;\n"
        "class ImageView;",
        "namespace views {\n"
        "class LabelButton;\n"
        "class ImageButton;  // XPLORER\n"
        "class ImageView;",
    )
    # New member: the open-chat button, next to editor_bubble_button_.
    edit(
        vtgh_h,
        "  const raw_ptr<views::LabelButton> editor_bubble_button_ = nullptr;",
        "  const raw_ptr<views::LabelButton> editor_bubble_button_ = nullptr;\n\n"
        "  // XPLORER: opens the Grok side panel to this group's chat\n"
        "  // conversation. Only shown for groups whose tabs are owned by a\n"
        '  // "chat:<conv_id>" agent (visibility toggled in OnDataChanged).\n'
        "  const raw_ptr<views::ImageButton> open_chat_button_ = nullptr;",
    )
    # New private method declaration.
    edit(
        vtgh_h,
        " private:\n"
        "  void UpdateEditorBubbleButtonVisibility();",
        " private:\n"
        "  // XPLORER: open the Grok side panel to this group's owning chat.\n"
        "  void OnOpenChatPressed();\n"
        "  void UpdateEditorBubbleButtonVisibility();",
    )

    vtgh_cc = (src / "chrome/browser/ui/views/tabs/vertical/"
               "vertical_tab_group_header_view.cc")
    # Includes (no BUILD.gn dep — final-link resolution, like grok_native.cc).
    edit(
        vtgh_cc,
        '#include "chrome/browser/ui/views/tabs/vertical/'
        'vertical_tab_group_header_view.h"',
        '#include "chrome/browser/ui/views/tabs/vertical/'
        'vertical_tab_group_header_view.h"\n'
        "\n"
        "// XPLORER: open-chat button — opens the Grok side panel to a\n"
        "// chat-owned tab group's conversation. These includes carry no GN dep\n"
        "// (final-link symbol resolution, the established fork pattern).\n"
        '#include "base/strings/string_util.h"  // XPLORER\n'
        '#include "chrome/browser/agent_gateway/tab_ownership.h"  // XPLORER\n'
        '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
        '#include "chrome/browser/ui/views/frame/browser_view.h"  // XPLORER\n'
        '#include "components/tabs/public/tab_group.h"  // XPLORER\n'
        '#include "components/tabs/public/tab_interface.h"  // XPLORER\n'
        '#include "content/public/browser/web_contents.h"  // XPLORER\n'
        '#include "ui/views/controls/button/image_button.h"  // XPLORER',
    )
    # File-local helper: the owning chat conv_id (or "") for a group, read from
    # the first tab's TabOwnership. The group title is the chat TOPIC now, so we
    # must NOT parse the title — read ownership from the tab instead.
    edit(
        vtgh_cc,
        "class VerticalTabGroupHeaderLabel : public views::Label {",
        "// XPLORER: returns the conv_id of the chat agent that owns |group|'s\n"
        '// tabs, or "" if the group is not chat-owned (Bookmarks / Scheduled /\n'
        "// organize / non-chat agent groups). The group title is the human topic\n"
        "// now, so read ownership from the first tab's TabOwnership rather than\n"
        "// parsing the title.\n"
        "std::string GetOwningChatConvId(const TabGroup& group) {\n"
        "  if (tabs::TabInterface* first = group.GetFirstTab()) {\n"
        "    if (content::WebContents* wc = first->GetContents()) {\n"
        "      if (agent_gateway::TabOwnership* own =\n"
        "              agent_gateway::TabOwnership::Get(wc)) {\n"
        '        if (base::StartsWith(own->owner, "chat:")) {\n'
        '          return own->owner.substr(5);  // strip "chat:"\n'
        "        }\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "  return std::string();\n"
        "}\n"
        "\n"
        "class VerticalTabGroupHeaderLabel : public views::Label {",
    )
    # Constructor init-list: AddChildView the open-chat ImageButton next to the
    # editor button (mirrors editor_bubble_button_'s bound-callback init).
    edit(
        vtgh_cc,
        "      editor_bubble_button_(AddChildView(std::make_unique<views::LabelButton>(\n"
        "          base::BindRepeating(&VerticalTabGroupHeaderView::ShowEditorBubble,\n"
        "                              base::Unretained(this))))),\n"
        "      collapse_icon_(AddChildView(std::make_unique<views::ImageView>())),",
        "      editor_bubble_button_(AddChildView(std::make_unique<views::LabelButton>(\n"
        "          base::BindRepeating(&VerticalTabGroupHeaderView::ShowEditorBubble,\n"
        "                              base::Unretained(this))))),\n"
        "      open_chat_button_(AddChildView(std::make_unique<views::ImageButton>(\n"
        "          base::BindRepeating(&VerticalTabGroupHeaderView::OnOpenChatPressed,\n"
        "                              base::Unretained(this))))),  // XPLORER\n"
        "      collapse_icon_(AddChildView(std::make_unique<views::ImageView>())),",
    )
    # Constructor body: configure the open-chat button (hidden until OnDataChanged
    # finds a chat owner). Spliced right after ConfigureEditorBubbleButton().
    edit(
        vtgh_cc,
        "  ConfigureEditorBubbleButton(editor_bubble_button_);",
        "  ConfigureEditorBubbleButton(editor_bubble_button_);\n"
        "  // XPLORER: open-chat button — hidden unless this is a chat-owned group.\n"
        '  open_chat_button_->SetTooltipText(u"Open chat");\n'
        '  open_chat_button_->GetViewAccessibility().SetName(u"Open chat");\n'
        "  open_chat_button_->SetImageHorizontalAlignment(\n"
        "      views::ImageButton::ALIGN_CENTER);\n"
        "  open_chat_button_->SetImageVerticalAlignment(\n"
        "      views::ImageButton::ALIGN_MIDDLE);\n"
        "  open_chat_button_->SetVisible(false);\n"
        "  open_chat_button_->SetProperty(\n"
        "      views::kFlexBehaviorKey,\n"
        "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
        "                               views::MaximumFlexSizeRule::kPreferred));",
    )
    # OnDataChanged: toggle visibility (chat-owned only) right after SetText.
    edit(
        vtgh_cc,
        "  group_header_label_->SetText(tab_group_visual_data_.title());",
        "  group_header_label_->SetText(tab_group_visual_data_.title());\n"
        "\n"
        "  // XPLORER: show the open-chat button only for chat-owned tab groups.\n"
        "  if (open_chat_button_) {\n"
        "    open_chat_button_->SetVisible(\n"
        "        !GetOwningChatConvId(delegate_->GetTabGroup()).empty());\n"
        "  }",
    )
    # OnDataChanged: tint the Grok icon like the header (inside the color block).
    edit(
        vtgh_cc,
        "    // Update editor bubble button.\n"
        "    UpdateEditorButtonColors(editor_bubble_button_, foreground_color);",
        "    // Update editor bubble button.\n"
        "    UpdateEditorButtonColors(editor_bubble_button_, foreground_color);\n"
        "\n"
        "    // XPLORER: tint the open-chat button's Grok icon to match the header.\n"
        "    open_chat_button_->SetImageModel(\n"
        "        views::Button::STATE_NORMAL,\n"
        "        ui::ImageModel::FromVectorIcon(kGrokIcon, foreground_color,\n"
        "                                       kIconSize));",
    )
    # OnOpenChatPressed: open the Grok side panel to the owning conversation.
    edit(
        vtgh_cc,
        "  editor_bubble_tracker_.Opened(delegate_->ShowGroupEditorBubble(\n"
        "      /*stop_context_menu_propagation=*/false));\n"
        "}\n"
        "\n"
        "BEGIN_METADATA(VerticalTabGroupHeaderView)",
        "  editor_bubble_tracker_.Opened(delegate_->ShowGroupEditorBubble(\n"
        "      /*stop_context_menu_propagation=*/false));\n"
        "}\n"
        "\n"
        "void VerticalTabGroupHeaderView::OnOpenChatPressed() {\n"
        "  // XPLORER: open the Grok side panel to this group's chat conversation.\n"
        "  const std::string conv = GetOwningChatConvId(delegate_->GetTabGroup());\n"
        "  if (conv.empty()) {\n"
        "    return;\n"
        "  }\n"
        "  views::Widget* widget = GetWidget();\n"
        "  if (!widget) {\n"
        "    return;\n"
        "  }\n"
        "  BrowserView* browser_view =\n"
        "      BrowserView::GetBrowserViewForNativeWindow(widget->GetNativeWindow());\n"
        "  if (browser_view && browser_view->browser()) {\n"
        "    grok_companion::OpenGrokSidePanelAt(browser_view->browser(),\n"
        '                                        "/?conv=" + conv);\n'
        "  }\n"
        "}\n"
        "\n"
        "BEGIN_METADATA(VerticalTabGroupHeaderView)",
    )

    # === XPLORER: bookmarks-settings gear button (sibling of open_chat_button_).
    # A second header button — a gear that opens the companion settings page to
    # the Bookmarks pane (/settings#bookmarks). Shown only on the native
    # "Bookmarks" group, identified by its first tab's bookmark_node_id (the
    # semantic marker, same spirit as open_chat_button_'s "chat:" owner check).
    # New .h member, declared AFTER open_chat_button_ so the ctor init order
    # (-Wreorder) stays: editor, open_chat, open_bookmarks_settings, collapse.
    edit(
        vtgh_h,
        "  const raw_ptr<views::ImageButton> open_chat_button_ = nullptr;",
        "  const raw_ptr<views::ImageButton> open_chat_button_ = nullptr;\n\n"
        "  // XPLORER: opens the companion settings page to the Bookmarks pane.\n"
        "  // Only shown for the native Bookmarks group (toggled in OnDataChanged).\n"
        "  const raw_ptr<views::ImageButton> open_bookmarks_settings_button_ =\n"
        "      nullptr;",
    )
    # New private method declaration.
    edit(
        vtgh_h,
        "  void OnOpenChatPressed();\n",
        "  void OnOpenChatPressed();\n"
        "  // XPLORER: open the companion settings page to the Bookmarks pane.\n"
        "  void OnManageBookmarksPressed();\n",
    )
    # Includes: kSettingsIcon + OpenXplorerSettings (no GN dep — final-link, like
    # the open-chat button's grok_companion include above).
    edit(
        vtgh_cc,
        '#include "ui/views/controls/button/image_button.h"  // XPLORER',
        '#include "ui/views/controls/button/image_button.h"  // XPLORER\n'
        '#include "chrome/browser/ui/views/xplorer/xplorer_settings_nav.h"  // XPLORER\n'
        '#include "components/vector_icons/vector_icons.h"  // XPLORER',
    )
    # File-local helper: is this the native Bookmarks group? Match the grouper's
    # title format ("Bookmarks (N)" from GroupTitle()). Title-based on purpose:
    # bookmark tabs may be UNLOADED (no WebContents/TabOwnership), so reading the
    # first tab's bookmark_node_id is unreliable; the group title is always set
    # by the time OnDataChanged runs.
    edit(
        vtgh_cc,
        "class VerticalTabGroupHeaderLabel : public views::Label {",
        "// XPLORER: true if |title| is the native Bookmarks group's title. The\n"
        '// grouper names it "Bookmarks (N)" via GroupTitle(); match that prefix.\n'
        "bool IsBookmarksGroup(const std::u16string& title) {\n"
        '  return base::StartsWith(title, u"Bookmarks (",\n'
        "                          base::CompareCase::SENSITIVE);\n"
        "}\n"
        "\n"
        "class VerticalTabGroupHeaderLabel : public views::Label {",
    )
    # Constructor init-list: AddChildView the gear button right after the
    # open-chat button (matches the .h declaration order).
    edit(
        vtgh_cc,
        "                              base::Unretained(this))))),  // XPLORER\n"
        "      collapse_icon_(AddChildView(std::make_unique<views::ImageView>())),",
        "                              base::Unretained(this))))),  // XPLORER\n"
        "      open_bookmarks_settings_button_(\n"
        "          AddChildView(std::make_unique<views::ImageButton>(\n"
        "              base::BindRepeating(\n"
        "                  &VerticalTabGroupHeaderView::OnManageBookmarksPressed,\n"
        "                  base::Unretained(this))))),  // XPLORER\n"
        "      collapse_icon_(AddChildView(std::make_unique<views::ImageView>())),",
    )
    # Constructor body: configure the gear button (hidden until OnDataChanged
    # finds the Bookmarks group). Spliced after the open-chat button's config.
    edit(
        vtgh_cc,
        "  open_chat_button_->SetProperty(\n"
        "      views::kFlexBehaviorKey,\n"
        "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
        "                               views::MaximumFlexSizeRule::kPreferred));",
        "  open_chat_button_->SetProperty(\n"
        "      views::kFlexBehaviorKey,\n"
        "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
        "                               views::MaximumFlexSizeRule::kPreferred));\n"
        "  // XPLORER: bookmarks-settings gear — hidden unless this is Bookmarks.\n"
        '  open_bookmarks_settings_button_->SetTooltipText(u"Manage bookmarks");\n'
        "  open_bookmarks_settings_button_->GetViewAccessibility().SetName(\n"
        '      u"Manage bookmarks");\n'
        "  open_bookmarks_settings_button_->SetImageHorizontalAlignment(\n"
        "      views::ImageButton::ALIGN_CENTER);\n"
        "  open_bookmarks_settings_button_->SetImageVerticalAlignment(\n"
        "      views::ImageButton::ALIGN_MIDDLE);\n"
        "  open_bookmarks_settings_button_->SetVisible(false);\n"
        "  open_bookmarks_settings_button_->SetProperty(\n"
        "      views::kFlexBehaviorKey,\n"
        "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
        "                               views::MaximumFlexSizeRule::kPreferred));",
    )
    # OnDataChanged: toggle the gear's visibility (native Bookmarks group only).
    edit(
        vtgh_cc,
        "  if (open_chat_button_) {\n"
        "    open_chat_button_->SetVisible(\n"
        "        !GetOwningChatConvId(delegate_->GetTabGroup()).empty());\n"
        "  }",
        "  if (open_chat_button_) {\n"
        "    open_chat_button_->SetVisible(\n"
        "        !GetOwningChatConvId(delegate_->GetTabGroup()).empty());\n"
        "  }\n"
        "  // XPLORER: show the gear only on the native Bookmarks group.\n"
        "  if (open_bookmarks_settings_button_) {\n"
        "    open_bookmarks_settings_button_->SetVisible(\n"
        "        IsBookmarksGroup(tab_group_visual_data_.title()));\n"
        "  }",
    )
    # OnDataChanged: tint the gear icon to match the header (in the color block).
    edit(
        vtgh_cc,
        "    open_chat_button_->SetImageModel(\n"
        "        views::Button::STATE_NORMAL,\n"
        "        ui::ImageModel::FromVectorIcon(kGrokIcon, foreground_color,\n"
        "                                       kIconSize));",
        "    open_chat_button_->SetImageModel(\n"
        "        views::Button::STATE_NORMAL,\n"
        "        ui::ImageModel::FromVectorIcon(kGrokIcon, foreground_color,\n"
        "                                       kIconSize));\n"
        "    open_bookmarks_settings_button_->SetImageModel(\n"
        "        views::Button::STATE_NORMAL,\n"
        "        ui::ImageModel::FromVectorIcon(vector_icons::kSettingsIcon,\n"
        "                                       foreground_color, kIconSize));",
    )
    # OnManageBookmarksPressed: open the settings page to the Bookmarks pane.
    edit(
        vtgh_cc,
        "BEGIN_METADATA(VerticalTabGroupHeaderView)",
        "void VerticalTabGroupHeaderView::OnManageBookmarksPressed() {\n"
        "  // XPLORER: open the companion settings page to the Bookmarks pane.\n"
        "  if (!IsBookmarksGroup(tab_group_visual_data_.title())) {\n"
        "    return;\n"
        "  }\n"
        "  views::Widget* widget = GetWidget();\n"
        "  if (!widget) {\n"
        "    return;\n"
        "  }\n"
        "  BrowserView* browser_view =\n"
        "      BrowserView::GetBrowserViewForNativeWindow(widget->GetNativeWindow());\n"
        "  if (browser_view && browser_view->browser()) {\n"
        "    xplorer::OpenXplorerSettings(browser_view->browser(), \"bookmarks\",\n"
        "                                 /*in_new_tab=*/true);\n"
        "  }\n"
        "}\n"
        "\n"
        "BEGIN_METADATA(VerticalTabGroupHeaderView)",
    )


# Release version. chrome/VERSION PATCH = MIN*100 + PAT (0.8.16 -> 816) so the
# Windows installer and Sparkle (CFBundleVersion) see every release as newer.
RELEASE_VERSION = "0.8.16"

# Every Chromium-file edit, captured from the reference macOS tree as one patch
# against the pin (after apply.sh's copy steps). The anchor edits below drifted
# from the tree (hand edits were never ported back), so a clean apply failed with
# ANCHOR NOT FOUND and Windows/Linux built without them. Regenerate with
# scripts/regen_chromium_patch.sh after changing Chromium files.
CHROMIUM_PATCH = Path(__file__).resolve().parent / "xplorer_chromium.diff"


def stamp_version(src: Path):
    ver = RELEASE_VERSION
    ss = src / "chrome/app/settings_strings.grdp"
    sst = ss.read_text(encoding="utf-8")
    sst2 = re.sub(r"Xplor [0-9][0-9.]* · Xplor", "Xplor " + ver + " · Xplor", sst, count=1)
    if sst2 != sst:
        ss.write_text(sst2, encoding="utf-8")
        print(f"  stamped about version -> {ver}")
    maj, minor, pat = (int(x) for x in ver.split("."))
    vf = src / "chrome/VERSION"
    v = vf.read_text()
    v2 = re.sub(r"(?m)^PATCH=\d+$", f"PATCH={minor * 100 + pat}", v)
    if v2 != v:
        vf.write_text(v2)
        print(f"  stamped chrome/VERSION PATCH -> {minor * 100 + pat}")


def apply_chromium_patch(src: Path) -> bool:
    """Applies xplorer_chromium.diff. Returns False if the file is absent."""
    if not CHROMIUM_PATCH.exists():
        return False
    base = ["git", "-C", str(src), "apply", "--whitespace=nowarn"]
    if subprocess.run(base + ["--check", str(CHROMIUM_PATCH)],
                      capture_output=True).returncode == 0:
        subprocess.run(base + [str(CHROMIUM_PATCH)], check=True)
        print(f"  applied {CHROMIUM_PATCH.name}")
    elif subprocess.run(base + ["--reverse", "--check",
                                "--exclude=chrome/VERSION",
                                "--exclude=chrome/app/settings_strings.grdp",
                                str(CHROMIUM_PATCH)],
                        capture_output=True).returncode == 0:
        # stamp_version rewrites those two files, so leave them out of the
        # "already applied" check.
        print(f"  {CHROMIUM_PATCH.name} already applied")
    else:
        r = subprocess.run(base + ["--check", str(CHROMIUM_PATCH)],
                           capture_output=True, text=True)
        sys.exit("xplorer_chromium.diff does not apply. Revert the chromium "
                 "tree to the pin (from inside it: git reset --hard HEAD; "
                 "git clean -fd chrome) and run apply again.\n" + r.stderr[-2000:])
    return True


def main(src: Path):
    if apply_chromium_patch(src):
        stamp_version(src)
        return
    legacy_main(src)


def legacy_main(src: Path):
    # 1. Start the AgentGateway once the browser UI is up.
    main_cc = src / "chrome/browser/chrome_browser_main.cc"
    # Must run after profile init (PostBrowserStart); earlier hooks crash on
    # unregistered prefs when the gateway resolves the profile dir.
    edit(
        main_cc,
        'TRACE_EVENT0("startup", "ChromeBrowserMainParts::PostBrowserStart");',
        f"\n  {MARKER}: start the AI-native agent gateway (HTTP 9334).\n"
        "  agent_gateway::AgentGateway::Start(0);\n",
    )
    edit(
        main_cc,
        '#include "chrome/browser/chrome_browser_main.h"',
        f'\n#include "chrome/browser/agent_gateway/agent_gateway.h"'
        f"  {MARKER}\n",
    )
    # 1b. Cleanly shut the gateway down in PostMainMessageLoopRun (after the main
    # loop quits, before thread teardown). The gateway is a leaked raw global
    # whose dtor never runs, so without this the AgentGateway server thread / its
    # net::HttpServer + listening socket / the Scheduler poll timer are never
    # torn down -> CompleteShutdown hangs and a zombie keeps port 9334 bound.
    # Anchor on the first Shutdown() in this method (UpgradeDetector), splicing
    # ours right after it — still well before browser_process_->StartTearDown().
    edit(
        main_cc,
        "  UpgradeDetector::GetInstance()->Shutdown();",
        f"\n\n  {MARKER}: deterministically stop the agent gateway server thread.\n"
        "  if (auto* g = agent_gateway::AgentGateway::GetInstance())\n"
        "    g->Shutdown();\n",
    )

    # 2. Link the component into chrome/browser.
    browser_gn = src / "chrome/browser/BUILD.gn"
    # Anchor on the unique warning comment inside static_library("browser")'s
    # public_deps block — the first bare `public_deps = [` in the file belongs
    # to a different target.
    edit(
        browser_gn,
        "public_deps = [\n    # WARNING WARNING WARNING",
        '\n    "//chrome/browser/agent_gateway",  # XPLORER',
    )

    # 3. Always start the CDP remote debugging server on 9333 — Xplorer treats
    # agents as first-class, no --remote-debugging-port flag needed. With no
    # switch present, default the port and let the existing policy-checked
    # startup path in GetInstance() do the rest.
    rds = src / "chrome/browser/devtools/remote_debugging_server.cc"
    edit(
        rds,
        "      command_line.GetSwitchValueASCII("
        "::switches::kRemoteDebuggingPort);",
        f"\n  {MARKER}: CDP always on for agents.\n"
        '  if (port_str.empty())\n    port_str = "9333";\n',
    )

    # 3b. AI-native runtime defaults: stop the browser from throttling or
    # suspending backgrounded/occluded tabs so an agent can drive many tabs at
    # full speed while Xplorer is inactive, and capture hidden windows. Injected
    # at the earliest startup hook (before FeatureList / GPU).
    cmd_delegate = src / "chrome/app/chrome_main_delegate.cc"
    edit(
        cmd_delegate,
        "std::optional<int> ChromeMainDelegate::BasicStartupComplete() {",
        f"\n  {MARKER}: AI-native defaults (never throttle backgrounded tabs) +\n"
        f"  {MARKER}: privacy — disable the component updater (Google pings), the\n"
        f"  {MARKER}: Finch field-trial config, and the Variations seed fetch\n"
        f"  {MARKER}: (empty server URLs). Brave/ungoogled-style: no phone-home.\n"
        "  {\n"
        "    base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();\n"
        '    for (const char* sw : {"disable-renderer-backgrounding",\n'
        '                           "disable-backgrounding-occluded-windows",\n'
        '                           "disable-background-timer-throttling",\n'
        '                           "disable-component-update",\n'
        '                           "disable-field-trial-config",\n'
        '                           "disable-domain-reliability",\n'
        '                           "disable-sync",\n'
        '                           "no-pings"}) {\n'
        "      if (!cmd->HasSwitch(sw))\n"
        "        cmd->AppendSwitch(sw);\n"
        "    }\n"
        '    for (const char* url_sw : {"variations-server-url",\n'
        '                               "variations-insecure-server-url"}) {\n'
        "      if (!cmd->HasSwitch(url_sw))\n"
        '        cmd->AppendSwitchASCII(url_sw, "");\n'
        "    }\n"
        '    if (!cmd->HasSwitch("disable-features"))\n'
        '      cmd->AppendSwitchASCII("disable-features",\n'
        '                             "CalculateNativeWinOcclusion,ChromeWhatsNewUI");\n'
        "  }\n",
    )

    # User-Agent / Sec-CH-UA: advertise the Xplorer brand alongside Chromium
    # (NEVER replace "Chromium"/"Google Chrome" or the Chrome/<ver> token —
    # site-compat). Appended in the shared brand-list builder so it shows in both
    # the low-entropy and full-version Sec-CH-UA brand lists. before=True with no
    # restated return line so edit() splices (the whitespace heuristic would
    # otherwise duplicate the return).
    ua_utils = src / "components/embedder_support/user_agent_utils.cc"
    edit(
        ua_utils,
        "  return ShuffleBrandList(brand_version_list, seed);",
        '  brand_version_list.emplace_back("Xplorer", version);  // XPLORER\n',
        before=True,
    )

    # 4. Branding: rename the product from "Chromium" to "Xplorer".
    branding = src / "chrome/app/theme/chromium/BRANDING"
    b = branding.read_text()
    if "PRODUCT_FULLNAME=Xplorer" not in b:
        for old in ("Chromium", "XBrowser"):
            b = b.replace(f"PRODUCT_FULLNAME={old}", "PRODUCT_FULLNAME=Xplorer")
            b = b.replace(f"PRODUCT_SHORTNAME={old}", "PRODUCT_SHORTNAME=Xplorer")
        b = b.replace("MAC_BUNDLE_ID=org.chromium.Chromium",
                      "MAC_BUNDLE_ID=org.xplorer.Xplorer")
        b = b.replace("MAC_BUNDLE_ID=org.xbrowser.XBrowser",
                      "MAC_BUNDLE_ID=org.xplorer.Xplorer")
        # Windows VERSIONINFO: CompanyName comes from these BRANDING keys (they
        # are baked into chrome.exe/chrome.dll via chrome_exe_version.rc.version).
        # Harmless on macOS, which keys off MAC_BUNDLE_ID / the product strings.
        b = b.replace("COMPANY_FULLNAME=The Chromium Authors",
                      "COMPANY_FULLNAME=Xplorer")
        b = b.replace("COMPANY_SHORTNAME=Chromium", "COMPANY_SHORTNAME=Xplorer")
        # Installer .exe file properties (setup.exe / mini_installer.exe
        # ProductName + FileDescription) — read "Chromium Installer" otherwise.
        b = b.replace("PRODUCT_INSTALLER_FULLNAME=Chromium Installer",
                      "PRODUCT_INSTALLER_FULLNAME=Xplorer Installer")
        b = b.replace("PRODUCT_INSTALLER_SHORTNAME=Chromium Installer",
                      "PRODUCT_INSTALLER_SHORTNAME=Xplorer Installer")
        branding.write_text(b)
        print(f"  edited: {branding}")

    # app-Info.plist hardcodes two UTTypeDescription strings as "Chromium
    # Extension"/"Chromium Shortcut" (all the other fields use the substituted
    # ${CHROMIUM_SHORT_NAME}, which resolves to "Xplorer"). Route these through
    # the same variable so Finder's Get-Info on .crx/app-shortcut files reads
    # "Xplorer …" instead of "Chromium …".
    app_info = src / "chrome/app/app-Info.plist"
    ai = app_info.read_text()
    if "${CHROMIUM_SHORT_NAME} Extension" not in ai:
        ai = ai.replace("<string>Chromium Extension</string>",
                        "<string>${CHROMIUM_SHORT_NAME} Extension</string>")
        ai = ai.replace("<string>Chromium Shortcut</string>",
                        "<string>${CHROMIUM_SHORT_NAME} Shortcut</string>")
        app_info.write_text(ai)
        print(f"  edited: {app_info}")

    # XPLORER: Finder/Dock DISPLAY name "Xplor". CFBundleDisplayName is the name
    # the Finder/Dock surface; pin it to the literal "Xplor". CFBundleName stays
    # ${CHROMIUM_SHORT_NAME} (= Xplorer) and the on-disk bundle remains
    # "Xplorer.app", so bundle identity / Sparkle updates are untouched.
    ai = app_info.read_text()
    if "<string>Xplor</string>" not in ai:
        ai = ai.replace(
            "\t<key>CFBundleDisplayName</key>\n"
            "\t<string>${EXECUTABLE_NAME}</string>\n",
            "\t<key>CFBundleDisplayName</key>\n"
            "\t<string>Xplor</string>\n")
        app_info.write_text(ai)
        print(f"  set CFBundleDisplayName=Xplor: {app_info}")

    # XPLORER: the macOS app-menu BOLD title and the auto "About <X>" item come
    # from CFBundleName (NOT CFBundleDisplayName or the grit product strings), so
    # pin CFBundleName to "Xplor" too for a fully-branded menu. Display-only: the
    # on-disk bundle ("Xplorer.app"), CFBundleIdentifier (org.xplorer.Xplorer),
    # the executable + framework/helper names (all from PRODUCT_FULLNAME), the
    # OSCrypt keychain service ("Chromium Safe Storage", a compile constant), and
    # Sparkle's identifier/version compare are all INDEPENDENT of CFBundleName, so
    # bundle identity / updates / keychain are untouched.
    ai = app_info.read_text()
    if "\t<key>CFBundleName</key>\n\t<string>${CHROMIUM_SHORT_NAME}</string>\n" in ai:
        ai = ai.replace(
            "\t<key>CFBundleName</key>\n"
            "\t<string>${CHROMIUM_SHORT_NAME}</string>\n",
            "\t<key>CFBundleName</key>\n"
            "\t<string>Xplor</string>\n")
        app_info.write_text(ai)
        print(f"  set CFBundleName=Xplor: {app_info}")

    # XPLORER: Sparkle 2.x auto-update keys. tweak_info_plist passes unknown
    # keys through to the built Info.plist unchanged, so writing them into the
    # template here is sufficient. SUFeedURL is the appcast — served from
    # xplor.sh (Cloudflare Worker, no-cache) so update notifications propagate
    # INSTANTLY (the legacy GitHub Pages feed had a 10-min cache that delayed +
    # confused update checks). The Pages feed stays alive for <=0.8.9 installs
    # that shipped pointing at it. SUPublicEDKey is the EdDSA public key the
    # updater verifies signatures against, and the AutomaticChecks/
    # ScheduledCheckInterval pair makes Sparkle self-check daily. NOTE:
    # deliberately no SUEnableInstallerLauncherService — the app is not
    # sandboxed, so the launcher XPC service must NOT be enabled.
    ai = app_info.read_text()
    if "SUFeedURL" not in ai:
        su_keys = (
            "\t<key>SUFeedURL</key>\n"
            "\t<string>https://xplor.sh/appcast.xml</string>\n"
            "\t<key>SUPublicEDKey</key>\n"
            "\t<string>1dT5/+AbAMKH6F1IrtejPfrplH9JVKDqMLGfhzQhaiI=</string>\n"
            "\t<key>SUEnableAutomaticChecks</key>\n"
            "\t<true/>\n"
            "\t<key>SUScheduledCheckInterval</key>\n"
            "\t<integer>86400</integer>\n"
        )
        ai = ai.replace("</dict>\n</plist>", su_keys + "</dict>\n</plist>")
        app_info.write_text(ai)
        print(f"  added Sparkle keys: {app_info}")

    # XPLORER: TCC usage descriptions so Screen Recording / Files & Folders
    # prompts show a proper reason (and can be requested at launch).
    ai = app_info.read_text()
    if "NSScreenCaptureUsageDescription" not in ai:
        privacy_keys = (
            "\t<key>NSScreenCaptureUsageDescription</key>\n"
            "\t<string>Xplor needs Screen Recording to capture your screen for Grok — "
            "region capture from the menu bar, image search, and page screenshots.</string>\n"
            "\t<key>NSDocumentsFolderUsageDescription</key>\n"
            "\t<string>Xplor can open documents you share with Grok.</string>\n"
            "\t<key>NSDesktopFolderUsageDescription</key>\n"
            "\t<string>Xplor can open files from your Desktop when you share them with Grok.</string>\n"
            "\t<key>NSDownloadsFolderUsageDescription</key>\n"
            "\t<string>Xplor can open files from Downloads when you share them with Grok.</string>\n"
            "\t<key>NSPhotoLibraryUsageDescription</key>\n"
            "\t<string>Xplor can attach photos you choose for Grok vision and chat.</string>\n"
        )
        ai = ai.replace("</dict>\n</plist>", privacy_keys + "</dict>\n</plist>")
        app_info.write_text(ai)
        print(f"  added TCC usage descriptions: {app_info}")

    # XPLORER: wire the Sparkle auto-updater into browser startup. Add the
    # include next to app_controller_mac.h, and kick off the updater right after
    # the controller marks startup complete in -applicationDidFinishLaunching:.
    app_controller = src / "chrome/browser/app_controller_mac.mm"
    t = app_controller.read_text()
    if "XplorerStartSparkleUpdater" not in t:
        t = t.replace(
            '#import "chrome/browser/app_controller_mac.h"\n',
            '#import "chrome/browser/app_controller_mac.h"\n'
            '#import "chrome/browser/xplorer_sparkle_updater.h"\n',
        )
        t = t.replace(
            "  _startupComplete = YES;\n",
            "  _startupComplete = YES;\n"
            "  XplorerStartSparkleUpdater();  // XPLORER: Sparkle auto-update\n"
        )
        app_controller.write_text(t)
        print(f"  edited: {app_controller}")

    # XPLORER: LINK Sparkle.framework via a dedicated source_set (runtime-loading
    # via NSBundle fails under the hardened runtime — [NSBundle load] returns NO;
    # linking emits the LC_LOAD_DYLIB + rpath at link time, the way Vivaldi/Brave
    # embed Sparkle). The glue lives in its OWN source_set so the vendored
    # framework's -F search path (framework_dirs) is applied ONLY to this one .mm,
    # NOT to all ~3000 files of static_library("browser") (a target-level
    # framework_dirs would change every file's compile command and force a full
    # chrome/browser rebuild). `frameworks`/`framework_dirs` still propagate
    # across the static-library boundary into the Xplorer Framework dylib (the
    # final link), so xplorer_sparkle_updater.mm gets <Sparkle/Sparkle.h> at
    # compile time and the binary gets -framework Sparkle (LC_LOAD_DYLIB
    # @rpath/Sparkle.framework/Versions/B/Sparkle) at link time. ARC is on by
    # default (default_compiler_configs); Sparkle 2.9.3 headers are -Werror-clean.
    # Injected into the existing standalone is_mac block alongside the other mac
    # source_sets (app_controller_mac etc.) that static_library("browser") deps.
    if 'source_set("xplorer_sparkle")' not in browser_gn.read_text():
        edit(
            browser_gn,
            'if (is_mac) {\n'
            '  source_set("chrome_browser_main_mac") {',
            'if (is_mac) {\n'
            '  # XPLORER: Sparkle auto-updater glue (linked, not runtime-loaded).\n'
            '  source_set("xplorer_sparkle") {\n'
            '    sources = [\n'
            '      "xplorer_sparkle_updater.h",\n'
            '      "xplorer_sparkle_updater.mm",\n'
            '    ]\n'
            '    frameworks = [ "Sparkle.framework" ]\n'
            '    framework_dirs = [ "//third_party/sparkle" ]\n'
            '  }\n\n'
            '  source_set("chrome_browser_main_mac") {',
        )
    # Pull the source_set into static_library("browser") on mac (next to the other
    # mac source_set deps). This carries Sparkle through to the final framework
    # link without putting the .mm in browser's own sources list.
    if '":xplorer_sparkle",' not in browser_gn.read_text():
        edit(
            browser_gn,
            '      ":app_controller_mac",\n',
            '      ":app_controller_mac",\n'
            '      ":xplorer_sparkle",  # XPLORER: linked Sparkle auto-updater\n',
        )

    # XPLORER: rpath so the linked Sparkle.framework resolves at runtime.
    # Chromium emits NO LC_RPATH on the Xplorer Framework binary (it references
    # sibling dylibs via @executable_path/... directly). Sparkle's dylib id is
    # @rpath/Sparkle.framework/Versions/B/Sparkle, so the loading binary (this
    # framework, which links //chrome/browser and thus carries the Sparkle
    # LC_LOAD_DYLIB) needs an rpath that resolves @rpath to Contents/Frameworks,
    # where build.sh / release_arch.sh stage Sparkle.framework. The chrome_app
    # executable lives at Contents/MacOS/, so @executable_path/../Frameworks ==
    # Contents/Frameworks. Added to mac_framework_bundle("chrome_framework")'s
    # ldflags (forwarded to its internal shared_library — the final link target).
    chrome_gn = src / "chrome/BUILD.gn"
    if "XPLORER: rpath for the linked Sparkle" not in chrome_gn.read_text():
        edit(
            chrome_gn,
            "    ldflags = [\n"
            '      "-compatibility_version",\n'
            "      chrome_dylib_version,\n"
            '      "-current_version",\n'
            "      chrome_dylib_version,\n"
            "    ]\n",
            "    ldflags = [\n"
            '      "-compatibility_version",\n'
            "      chrome_dylib_version,\n"
            '      "-current_version",\n'
            "      chrome_dylib_version,\n"
            "      # XPLORER: rpath for the linked Sparkle.framework\n"
            "      # (id @rpath/Sparkle.framework/...) -> Contents/Frameworks.\n"
            "      # @loader_path is RELATIVE TO THIS FRAMEWORK BINARY (same for the\n"
            "      # browser process AND every helper that loads it), so it resolves\n"
            "      # for all process types; @executable_path/../Frameworks only works\n"
            "      # for the main exe (helpers live deeper -> their ../Frameworks is\n"
            "      # empty -> dlopen crash). Binary is at\n"
            "      # Contents/Frameworks/Xplorer Framework.framework/Versions/<V>/, so\n"
            "      # @loader_path/../../.. == Contents/Frameworks. Keep both.\n"
            '      "-Wl,-rpath,@loader_path/../../..",\n'
            '      "-Wl,-rpath,@executable_path/../Frameworks",\n'
            "    ]\n",
        )

    # The visible app name comes from IDS_PRODUCT_NAME / IDS_SHORT_PRODUCT_NAME
    # in the (non-Google, non-CfT) else branch of chromium_strings.grd.
    grd = src / "chrome/app/chromium_strings.grd"
    g = grd.read_text()
    if ">\n            Xplor\n" not in g:
        for old in ("Chromium", "XBrowser"):
            g = g.replace(
                'desc="The Chrome application name" translateable="false">\n'
                f"            {old}\n",
                'desc="The Chrome application name" translateable="false">\n'
                "            Xplor\n",
            )
            g = g.replace(
                'desc="The Chrome application short name." translateable="false">\n'
                f"            {old}\n",
                'desc="The Chrome application short name." translateable="false">\n'
                "            Xplor\n",
            )
        grd.write_text(g)
        print(f"  edited: {grd}")

    # The window/tab/accessible title formats hardcode "- Chromium" (they do NOT
    # use the renamed product placeholder), so titles read "<page> - Chromium".
    # Rebrand the title-format messages: covers the stable browser title, the
    # macOS accessible title + channel variants (Beta/Dev/Canary), and the
    # ChromeOS / captive-portal layouts.
    g = grd.read_text()
    if "</ph> - Xplor" not in g:
        g = g.replace("</ph> - Chromium", "</ph> - Xplor")
        g = g.replace("Chromium - <ph", "Xplor - <ph")
        g = g.replace("- Network Sign-in - Chromium", "- Network Sign-in - Xplor")
        g = g.replace("Chromium - Network Sign-in", "Xplor - Network Sign-in")
        grd.write_text(g)
        print(f"  edited (title formats): {grd}")

    # Broad app-name rebrand: ~700 user-facing strings in chromium_strings.grd
    # still hardcode "Chromium" (default-browser prompt, profile/startup errors,
    # background-run, update nags, etc.) — the product-name rename only covered
    # IDS_PRODUCT_NAME. Replace them all with Xplorer, preserving the legal
    # "Chromium Authors" copyright. Risk-checked: no <message name="…"> IDs
    # contain "Chromium", so this only touches text content + translator descs,
    # never resource IDs. Guard: post-rebrand only the copyright keeps "Chromium".
    g = grd.read_text()
    if g.count("Chromium") > g.count("Chromium Authors"):
        g = g.replace("Chromium Authors", "\x00AUTH\x00")
        g = g.replace("Chromium", "Xplor")
        g = g.replace("\x00AUTH\x00", "Chromium Authors")
        grd.write_text(g)
        print(f"  edited (broad app-name rebrand): {grd}")

    # Extend the same safe rebrand to every other user-facing strings file —
    # settings (148), omnibox pedals (40), components, search-engine choice,
    # privacy sandbox, password manager, page info, autofill, … ~300 more
    # hardcoded "Chromium" app-name refs. Glob *strings.grd/.grdp only (skips
    # resource/image grds); generated_resources.grd uses PRODUCT_NAME subst so
    # only its few literals need touching.
    for base, pat in (("chrome/app", "*strings.grd"), ("chrome/app", "*strings.grdp"),
                      ("components", "**/*strings.grd"), ("components", "**/*strings.grdp")):
        for f in sorted((src / base).glob(pat)):
            rebrand_grd_strings(f)
    rebrand_grd_strings(src / "chrome/app/generated_resources.grd")

    # 5. Link Grok companion (side panel + AI Mode redirect).
    edit(
        browser_gn,
        '\n    "//chrome/browser/agent_gateway",  # XPLORER',
        '\n    "//chrome/browser/grok_companion",  # XPLORER',
    )

    # AI Mode omnibox chip -> open native Grok Search page (not Google AI Mode).
    # M151 had a dedicated icon view. M153 folded that into the page-action
    # framework (click still lands in AiModePageActionController::OpenAiMode),
    # so these edits only apply when the old file is present.
    ai_mode_icon = src / "chrome/browser/ui/views/location_bar/ai_mode_page_action_icon_view.cc"
    if ai_mode_icon.exists():
        edit(
            ai_mode_icon,
            'void AiModePageActionIconView::OnExecuting(\n'
            '    PageActionIconView::ExecuteSource source) {\n'
            '  OmniboxController* omnibox_controller =\n'
            '      search::GetOmniboxController(GetWebContents());\n'
            '  CHECK(omnibox_controller);\n'
            '  omnibox::AiModePageActionController::OpenAiMode(*omnibox_controller,\n'
            '                                                  /*via_keyboard=*/false);\n'
            '}',
            'void AiModePageActionIconView::OnExecuting(\n'
            '    PageActionIconView::ExecuteSource source) {\n'
            '  // XPLORER: Grok chip opens native Grok Search.\n'
            '  grok_companion::OpenGrokSearchPage(browser_);\n'
            '}',
        )
        edit(
            ai_mode_icon,
            '    omnibox::AiModePageActionController::OpenAiMode(*omnibox_controller,\n'
            '                                                    /*via_keyboard=*/true);\n'
            '    return true;',
            '    grok_companion::OpenGrokSearchPage(browser_);\n'
            '    return true;',
        )
        edit(
            ai_mode_icon,
            '  SetUseTonalColorsWhenExpanded(true);\n'
            '  SetBackgroundVisibility(BackgroundVisibility::kWithLabel);\n'
            '}',
            '  SetUseTonalColorsWhenExpanded(true);\n'
            '  SetBackgroundVisibility(BackgroundVisibility::kWithLabel);\n'
            '  SetTooltipText(u"Open Grok Search");\n'
            '}',
        )
        edit(
            ai_mode_icon,
            '#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"',
            '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n',
        )
    else:
        print(f"  skip (removed upstream): {ai_mode_icon.name}")
    # Redirect OpenAiMode (command callback) to Grok Search.
    ai_mode_ctrl = src / "chrome/browser/ui/omnibox/ai_mode_page_action_controller.cc"
    edit(
        ai_mode_ctrl,
        'void AiModePageActionController::OpenAiMode(\n'
        '    OmniboxController& omnibox_controller,\n'
        '    bool via_keyboard) {\n'
        '  omnibox_controller.edit_model()->OpenAiMode(\n'
        '      via_keyboard ? OmniboxEditModel::AimActivation::kKeyboard\n'
        '                   : OmniboxEditModel::AimActivation::kClickOrGesture);\n'
        '}',
        'void AiModePageActionController::OpenAiMode(\n'
        '    OmniboxController& omnibox_controller,\n'
        '    bool via_keyboard) {\n'
        '  // XPLORER: never open Google AI Mode — use native Grok Search.\n'
        '  OmniboxClient* client = omnibox_controller.client();\n'
        '  if (!client || !client->IsChromeOmniboxClient()) {\n'
        '    return;\n'
        '  }\n'
        '  Browser* browser = static_cast<ChromeOmniboxClient*>(client)->browser();\n'
        '  if (browser) {\n'
        '    grok_companion::OpenGrokSearchPage(browser);\n'
        '  }\n'
        '}',
    )
    edit(
        ai_mode_ctrl,
        '#include "chrome/browser/ui/omnibox/ai_mode_page_action_controller.h"',
        '#include "chrome/browser/ui/omnibox/ai_mode_page_action_controller.h"\n'
        '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
        '#include "chrome/browser/ui/browser.h"  // XPLORER\n'
        '#include "chrome/browser/ui/omnibox/chrome_omnibox_client.h"  // XPLORER\n',
    )
    edit(
        ai_mode_ctrl,
        'bool AiModePageActionController::ShouldShowPageAction(\n'
        '    Profile* profile,\n'
        '    LocationBar& location_bar) {',
        'bool AiModePageActionController::ShouldShowPageAction(\n'
        '    Profile* profile,\n'
        '    LocationBar& location_bar) {\n'
        '  // XPLORER: always show Grok entrypoint in Xplorer.\n'
        '  if (profile && profile->IsRegularProfile()) {\n'
        '    return true;\n'
        '  }',
    )

    # Register grok.com toolbar overlay early (before side panel / NTP race).
    browser_features = (
        src / "chrome/browser/ui/browser_window/internal/browser_window_features.cc")
    edit(
        browser_features,
        '#include "chrome/browser/ui/views/side_panel/side_panel_coordinator.h"\n',
        '#include "chrome/browser/grok_companion/grok_fab.h"  // XPLORER\n'
        '#include "chrome/browser/grok_companion/grok_web_bar.h"  // XPLORER\n',
        before=True,
    )
    edit(
        browser_features,
        '  // TODO(crbug.com/346148093): Move SidePanelCoordinator construction to Init.',
        '  // XPLORER: grok.com/grokipedia toolbar before side panel init (NTP race).\n'
        '  grok_companion::RegisterGrokWebBar(browser);\n'
        '  grok_companion::RegisterGrokFab(browser);\n\n'
        '  // TODO(crbug.com/346148093): Move SidePanelCoordinator construction to Init.',
    )

    # Register Grok side panel in global entries.
    side_panel_helper = (
        src / "chrome/browser/ui/views/side_panel/side_panel_helper.cc")
    edit(
        side_panel_helper,
        '#include "chrome/browser/ui/views/side_panel/reading_list/reading_list_side_panel_coordinator.h"',
        '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
        '#include "chrome/browser/grok_companion/grok_fab.h"  // XPLORER\n'
        '#include "chrome/browser/grok_companion/grok_web_bar.h"  // XPLORER\n',
    )
    edit(
        side_panel_helper,
        '  // Add bookmarks.\n'
        '  BookmarksSidePanelCoordinator::From(browser)->CreateAndRegisterEntry(\n'
        '      window_registry);',
        '  // Add bookmarks.\n'
        '  BookmarksSidePanelCoordinator::From(browser)->CreateAndRegisterEntry(\n'
        '      window_registry);\n\n'
        '  // XPLORER: Grok AI companion side panel + grok.com toolbar overlay.\n'
        '  grok_companion::RegisterGrokWebBar(browser);\n'
        '  grok_companion::RegisterGrokFab(browser);\n'
        '  grok_companion::RegisterGrokSidePanel(browser);',
    )

    # New tab page -> Grok search homepage is handled by Browser::GetNewTabURL
    # (below) + grok_companion's legacy-NTP redirect. We deliberately do NOT
    # patch search.cc's GetNewTabPageURL: an unconditional early return there
    # makes the rest of the function unreachable, which fails -Werror.

    # New tabs must navigate to the Grok home URL directly — not chrome://newtab
    # (injectors only run on http/https pages). M153 moved this out of
    # Browser::GetNewTabURL into chrome::GetNewTabURL (browser_tabstrip.cc).
    tabstrip_cc = src / "chrome/browser/ui/browser_tabstrip.cc"
    edit(
        tabstrip_cc,
        '#include "chrome/browser/ui/browser_tabstrip.h"',
        '#include "chrome/browser/ui/browser_tabstrip.h"\n'
        '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n',
    )
    edit(
        tabstrip_cc,
        'GURL GetNewTabURL(const BrowserWindowInterface* browser) {\n'
        '  if (browser) {\n'
        '    if (auto* const app_browser_controller =\n'
        '            web_app::AppBrowserController::From(browser)) {\n'
        '      return app_browser_controller->GetAppNewTabUrl();\n'
        '    }\n'
        '  }\n'
        '  return ChromeUINewTabURLAsGURL();\n}',
        'GURL GetNewTabURL(const BrowserWindowInterface* browser) {\n'
        '  if (browser) {\n'
        '    if (auto* const app_browser_controller =\n'
        '            web_app::AppBrowserController::From(browser)) {\n'
        '      return app_browser_controller->GetAppNewTabUrl();\n'
        '    }\n'
        '  }\n'
        '  // XPLORER: open Grok home directly so page injectors can attach.\n'
        '  return grok_companion::GetStartupHomeURL();\n}',
    )

    tab_restore_client = (
        src / "chrome/browser/sessions/chrome_tab_restore_service_client.cc")
    edit(
        tab_restore_client,
        '#include "chrome/browser/sessions/chrome_tab_restore_service_client.h"',
        '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n',
    )
    edit(
        tab_restore_client,
        'GURL ChromeTabRestoreServiceClient::GetNewTabURL() {\n'
        '  return chrome::ChromeUINewTabURLAsGURL();\n}',
        'GURL ChromeTabRestoreServiceClient::GetNewTabURL() {\n'
        '  // XPLORER: match Browser::GetNewTabURL().\n'
        '  return grok_companion::GetStartupHomeURL();\n}',
    )

    # New tabs land on the Grok home (an http gateway page). Render it with a
    # blank omnibox like the NTP instead of exposing the internal gateway URL.
    loc_delegate = (
        src / "chrome/browser/ui/toolbar/chrome_location_bar_model_delegate.cc")
    edit(
        loc_delegate,
        '#include "components/search/ntp_features.h"',
        '#include "components/search/ntp_features.h"\n'
        '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER',
    )
    edit(
        loc_delegate,
        '  GURL url = entry->GetURL();\n'
        '  if (is_ntp(entry->GetVirtualURL()) || is_ntp(url)) {\n'
        '    return false;\n'
        '  }',
        '  GURL url = entry->GetURL();\n'
        '  if (is_ntp(entry->GetVirtualURL()) || is_ntp(url)) {\n'
        '    return false;\n'
        '  }\n'
        '\n'
        '  // XPLORER: our own gateway home pages (the new-tab home) get a blank\n'
        '  // omnibox, just like the NTP.\n'
        '  if (grok_companion::IsGrokHomeURL(url) ||\n'
        '      grok_companion::IsGrokHomeURL(entry->GetVirtualURL())) {\n'
        '    return false;\n'
        '  }',
    )

    # Enable AI Mode omnibox entrypoint feature flag.
    edit(
        cmd_delegate,
        '      cmd->AppendSwitchASCII("disable-features",\n'
        '                             "CalculateNativeWinOcclusion,ChromeWhatsNewUI");\n'
        '  }\n',
        '      cmd->AppendSwitchASCII("disable-features",\n'
        '                             "CalculateNativeWinOcclusion,ChromeWhatsNewUI");\n'
        '    if (!cmd->HasSwitch("enable-features"))\n'
        '      cmd->AppendSwitchASCII("enable-features",\n'
        '                             "AiModeOmniboxEntryPoint");\n'
        '    if (!cmd->HasSwitch("top-chrome-touch-ui"))\n'
        '      cmd->AppendSwitchASCII("top-chrome-touch-ui", "enabled");\n'
        '  }\n',
    )

    # XPLORER: vertical tabs (tabs to the side) on by default — enable the
    # feature + expand-on-hover, and flip the layout pref so a fresh profile
    # opens with the side tab strip rather than the horizontal strip.
    tabs_features = src / "chrome/browser/ui/tabs/features.cc"
    # NOTE: edit() only *replaces* the anchor when the insertion restates its
    # first or last line; a bare one-line value flip is treated as additive and
    # gets spliced (duplicated). So anchor each flip together with an unchanged
    # neighbouring line.
    edit(
        tabs_features,
        "BASE_FEATURE(kVerticalTabs, base::FEATURE_DISABLED_BY_DEFAULT);\n\n"
        "BASE_FEATURE(kVerticalTabsLaunch, base::FEATURE_ENABLED_BY_DEFAULT);",
        "BASE_FEATURE(kVerticalTabs, base::FEATURE_ENABLED_BY_DEFAULT);\n\n"
        "BASE_FEATURE(kVerticalTabsLaunch, base::FEATURE_ENABLED_BY_DEFAULT);",
    )
    edit(
        tabs_features,
        "BASE_FEATURE(kVerticalTabsExpandOnHover, "
        "base::FEATURE_DISABLED_BY_DEFAULT);\nBASE_FEATURE_PARAM(bool,",
        "BASE_FEATURE(kVerticalTabsExpandOnHover, "
        "base::FEATURE_ENABLED_BY_DEFAULT);\nBASE_FEATURE_PARAM(bool,",
    )
    tab_strip_prefs = src / "chrome/browser/ui/tabs/tab_strip_prefs.cc"
    edit(
        tab_strip_prefs,
        "  registry->RegisterBooleanPref(prefs::kVerticalTabsEnabled, false);\n"
        "  registry->RegisterBooleanPref(\n"
        "      prefs::kVerticalTabsExpandOnHoverEnabled,",
        "  registry->RegisterBooleanPref(prefs::kVerticalTabsEnabled, true);\n"
        "  registry->RegisterBooleanPref(\n"
        "      prefs::kVerticalTabsExpandOnHoverEnabled,",
    )

    # Rename "AI Mode" label to "Grok" in the omnibox chip.
    g = grd.read_text()
    if "IDS_AI_MODE_ENTRYPOINT_LABEL" in g and ">Grok<" not in g:
        g = g.replace(
            '<message name="IDS_AI_MODE_ENTRYPOINT_LABEL"\n'
            '        desc="The label of the AI mode entrypoint in the omnibox" formatter_data="android_java">\n'
            '        AI Mode\n'
            '      </message>',
            '<message name="IDS_AI_MODE_ENTRYPOINT_LABEL"\n'
            '        desc="The label of the AI mode entrypoint in the omnibox" formatter_data="android_java">\n'
            '        Grok\n'
            '      </message>',
        )
        grd.write_text(g)
        print(f"  edited: {grd}")

    # Attach Grok page injectors to every tab at creation time.
    tab_helpers = src / "chrome/browser/ui/tab_helpers.cc"
    edit(
        tab_helpers,
        '#include "chrome/browser/ui/tab_helpers.h"',
        '\n#include "chrome/browser/grok_companion/grok_fab.h"  // XPLORER\n'
        '#include "chrome/browser/grok_companion/grok_web_bar.h"  // XPLORER\n',
    )
    edit(
        tab_helpers,
        '  // --- Section 1: Common tab helpers ---',
        '  // XPLORER: Grok toolbar + floating page button on every regular tab.\n'
        '  if (profile && profile->IsRegularProfile()) {\n'
        '    grok_companion::AttachGrokWebBarInjector(web_contents);\n'
        '    grok_companion::AttachGrokFabInjector(web_contents);\n'
        '  }\n\n'
        '  // --- Section 1: Common tab helpers ---',
    )

    # Toolbar Grok button (top-right icon that toggles the Grok agent side
    # panel — the native sidebar where you chat with Grok and it drives the
    # browser via MCP). Grok logo icon.
    toolbar = src / "chrome/browser/ui/views/toolbar/toolbar_view.cc"
    grok_btn_block = (
        '  // XPLORER: Grok companion toolbar button.\n'
        '  {\n'
        '    auto grok_btn = std::make_unique<ToolbarButton>(base::BindRepeating(\n'
        '        [](Browser* b) {\n'
        '          if (b)\n'
        '            grok_companion::ToggleGrokSidePanel(b);\n'
        '        },\n'
        '        base::Unretained(browser_)));\n'
        '    grok_btn->SetTooltipText(u"Ask Grok");\n'
        '    grok_btn->SetAccessibleName(u"Ask Grok");\n'
        '    grok_btn->SetVectorIcon(kGrokIcon);\n'
        '    grok_btn->SetProperty(views::kElementIdentifierKey,\n'
        '                          kToolbarGrokButtonElementId);\n'
        '    AddChildView(std::move(grok_btn));\n'
        '  }\n\n'
    )
    if False:  # XPLORER: explicit Grok button dropped; the default-pinned
        # kSearchCompanion side-panel button is the single always-visible toggle.
        if "ToggleGrokSidePanel" in toolbar.read_text():
            edit(
                toolbar,
                '  // XPLORER: Grok companion toolbar button.\n'
                '  {\n'
                '    auto grok_btn = std::make_unique<ToolbarButton>(base::BindRepeating(\n'
                '        [](Browser* b) {\n'
                '          if (b)\n'
                '            grok_companion::ToggleGrokSidePanel(b);\n'
                '        },\n'
                '        base::Unretained(browser_)));\n'
                '    grok_btn->SetTooltipText(u"Grok");\n'
                '    grok_btn->SetAccessibleName(u"Grok");\n'
                '    grok_btn->SetVectorIcon(vector_icons::kLightbulbIcon);\n'
                '    grok_btn->SetProperty(views::kElementIdentifierKey,\n'
                '                          kToolbarGrokButtonElementId);\n'
                '    AddChildView(std::move(grok_btn));\n'
                '  }\n\n',
                grok_btn_block,
            )
            edit(
                toolbar,
                '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
                '#include "components/vector_icons/vector_icons.h"\n'
                '#include "chrome/browser/ui/views/toolbar/toolbar_button.h"\n',
                '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
                '#include "chrome/app/vector_icons/vector_icons.h"  // XPLORER\n'
                '#include "chrome/browser/ui/views/toolbar/toolbar_button.h"\n',
            )
        elif "GetGrokToolbarIcon" in toolbar.read_text():
            edit(
                toolbar,
                '    grok_btn->SetImageModel(views::Button::STATE_NORMAL,\n'
                '                            grok_companion::GetGrokToolbarIcon());\n'
                '    grok_btn->SetImageModel(views::Button::STATE_HOVERED,\n'
                '                            grok_companion::GetGrokToolbarIcon());\n'
                '    grok_btn->SetImageModel(views::Button::STATE_PRESSED,\n'
                '                            grok_companion::GetGrokToolbarIcon());\n',
                '    grok_btn->SetVectorIcon(kGrokIcon);\n',
            )
            edit(
                toolbar,
                '#include "chrome/browser/grok_companion/grok_toolbar_icon.h"  // XPLORER\n',
                '#include "chrome/app/vector_icons/vector_icons.h"  // XPLORER\n',
            )
        else:
            # Splice the Grok button BEFORE the overflow button — purely additive.
            # Do NOT restate the overflow_button_ line in the insertion: edit()'s
            # "rewrite vs splice" heuristic compares anchor.strip() (de-indented)
            # against the insertion's still-indented lines, so the last-line match
            # misses and it splices additively. Restating the line then leaves a
            # SECOND, visible, controller-less OverflowButton whose RunMenu() calls
            # ToolbarController::ShowMenu() on a null controller → crash on click.
            edit(
                toolbar,
                '  overflow_button_ = AddChildView(std::make_unique<OverflowButton>());',
                grok_btn_block,
                before=True,
            )
            edit(
                toolbar,
                '#include "chrome/browser/ui/views/toolbar/toolbar_view.h"',
                '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
                '#include "chrome/app/vector_icons/vector_icons.h"  // XPLORER\n'
                '#include "chrome/browser/ui/views/toolbar/toolbar_button.h"\n',
            )
    # Grok logo vector icon for toolbar button.
    vector_icons_gn = src / "chrome/app/vector_icons/BUILD.gn"
    edit(
        vector_icons_gn,
        '    "grid_view.icon",\n',
        '    "grid_view.icon",\n    "grok.icon",\n',
    )

    # Element id for the Grok toolbar button (local to toolbar_view.cc).
    browser_elements = src / "chrome/browser/ui/browser_element_identifiers.h"
    edit(
        browser_elements,
        'DECLARE_ELEMENT_IDENTIFIER_VALUE(kLocationBarElementId);',
        'DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarGrokButtonElementId);\n',
    )
    browser_elements_cc = src / "chrome/browser/ui/browser_element_identifiers.cc"
    edit(
        browser_elements_cc,
        'DEFINE_ELEMENT_IDENTIFIER_VALUE(kLocationBarElementId);',
        'DEFINE_ELEMENT_IDENTIFIER_VALUE(kToolbarGrokButtonElementId);\n',
    )

    # Register an actions::ActionItem for the Grok companion side panel. The
    # native side-panel header controller calls GetActionItem(entry->key()) and
    # unconditionally AddActionChangedCallback() on it. For the reused
    # kSearchCompanion id Chrome only registers that ActionItem when the (absent)
    # companion feature is enabled, so without this the header derefs null and the
    # browser SIGSEGVs the first time the Grok side panel opens (the "Ask Grok"
    # toolbar button). Reuse the Grok icon + the branded "Grok" label.
    browser_actions = src / "chrome/browser/ui/browser_actions.cc"
    edit(
        browser_actions,
        "  BrowserWindowInterface* const bwi = base::to_address(bwi_);\n",
        "  BrowserWindowInterface* const bwi = base::to_address(bwi_);\n\n"
        "  // XPLORER: register the Grok companion side-panel action so the\n"
        "  // side-panel header controller finds a non-null ActionItem.\n"
        "  root_action_item_->AddChild(\n"
        "      SidePanelAction(SidePanelEntryId::kSearchCompanion,\n"
        "                      IDS_AI_MODE_ENTRYPOINT_LABEL,\n"
        "                      IDS_AI_MODE_ENTRYPOINT_LABEL, kGrokIcon,\n"
        "                      kActionSidePanelShowSearchCompanion, bwi, true)\n"
        "          .Build());\n",
    )

    # XPLORER: pin the Grok side-panel button by default so it is the single,
    # always-visible Grok toggle — shown in BOTH the inactive (panel closed) and
    # active (panel open, highlighted) states. The native side-panel button is
    # only shown ephemerally when active, so without this the button vanishes
    # when the panel is closed. Inserted before the CanUpdate gate so it applies
    # regardless of toolbar customization; UpdatePinnedState is idempotent.
    pinned_model = src / ("chrome/browser/ui/toolbar/pinned_toolbar/"
                          "pinned_toolbar_actions_model.cc")
    edit(
        pinned_model,
        "void PinnedToolbarActionsModel::MaybeMigrateExistingPinnedStates() {\n"
        "  if (!CanUpdate()) {\n"
        "    return;\n"
        "  }\n",
        "void PinnedToolbarActionsModel::MaybeMigrateExistingPinnedStates() {\n"
        "  // XPLORER: keep the Grok side-panel button pinned (always visible).\n"
        "  UpdatePinnedState(kActionSidePanelShowSearchCompanion, true);\n"
        "  if (!CanUpdate()) {\n"
        "    return;\n"
        "  }\n",
    )

    # --- Grok as the default search engine --------------------------------
    # Repoint the prepopulated "google" fallback entry (the first-run default,
    # keyed on id==1) at the gateway GET /omnibox 302 handoff, so typing a
    # query in the address bar searches Grok. The engine short_name "Grok"
    # also makes the omnibox placeholder read "Search Grok or type a URL".
    # suggest_url is emptied to drop Google autocomplete suggestions, and
    # kCurrentDataVersion is bumped so existing profiles re-merge the change.
    prepop = src / ("third_party/search_engines_data/resources/definitions/"
                    "prepopulated_engines.json")
    pp = prepop.read_text()
    if "127.0.0.1:9334/omnibox" not in pp:
        # name + keyword: whitespace-tolerant regex with a hard assert. The old
        # literal .replace() silently no-ops on any format drift (unlike edit()),
        # which would ship Google as the default search engine. Fail LOUD here so
        # an upstream format change is caught at patch time, not in the binary.
        pp, n = re.subn(
            r'"name":\s*"Google",\s*"keyword":\s*"google\.com",',
            '"name": "Grok",\n      "keyword": "grok.com",', pp)
        if n != 1:
            sys.exit("prepopulated_engines.json: Google name/keyword anchor "
                     f"matched {n}x (expected 1) — upstream format moved")
        pp = re.sub(
            r'"search_url": "\{google:baseURL\}search\?q=\{searchTerms\}[^"]*"',
            '"search_url": "http://127.0.0.1:9334/omnibox?q={searchTerms}"', pp)
        pp = re.sub(r'"suggest_url": "\{google:baseSuggestURL\}[^"]*"',
                    '"suggest_url": ""', pp)
        # The engine favicon was left pointing at Google; repoint it at Grok,
        # scoped to the now-"Grok" block (count=1) so no other engine is touched.
        pp = re.sub(
            r'("name": "Grok",[\s\S]{0,400}?"favicon_url": )"[^"]*"',
            r'\1"https://grok.com/favicon.ico"', pp, count=1)
        # Bump kCurrentDataVersion to current+1 so existing profiles re-merge.
        # (The old hardcoded "206"->"207" was a silent no-op on M151, which
        # already ships 207 — read the live value and increment it instead.)
        m = re.search(r'"kCurrentDataVersion":\s*(\d+)', pp)
        if not m:
            sys.exit("prepopulated_engines.json: kCurrentDataVersion not found")
        pp = re.sub(r'"kCurrentDataVersion":\s*\d+',
                    f'"kCurrentDataVersion": {int(m.group(1)) + 1}', pp, count=1)
        prepop.write_text(pp)
        print(f"  edited: {prepop}")

    # --- About page: "About Xplorer" + no failed-update error -------------
    grdp = src / "chrome/app/settings_chromium_strings.grdp"
    sg = grdp.read_text()
    if "About Xplor" not in sg:
        sg = sg.replace(
            'desc="Menu title for the About Chromium page.">\n'
            "        About Chromium\n",
            'desc="Menu title for the About Chromium page.">\n'
            "        About Xplor\n")
        sg = sg.replace(
            'desc="Text of the button which takes the user to the Chrome help'
            ' page.">\n        Get help with Chromium\n',
            'desc="Text of the button which takes the user to the Chrome help'
            ' page.">\n        Get help with Xplor\n')
        sg = sg.replace(
            'desc="Status label: Already up to date (Chromium)">\n'
            "      Chromium is up to date\n",
            'desc="Status label: Already up to date (Chromium)">\n'
            "      Xplor is up to date\n")
        grdp.write_text(sg)
        print(f"  edited: {grdp}")

    # --- About page version line: lead with the Xplorer version --------------
    # The about/help line is the Chromium ENGINE version ("Version 151.0.7897.0
    # (Developer Build) ..."). Prepend the Xplorer product version so users see
    # OUR version first. NOTE: bump XPLORER_VERSION here per release (or wire it
    # to the release version later).
    XPLORER_VERSION = "0.8.14"
    ss = src / "chrome/app/settings_strings.grdp"
    sst = ss.read_text()
    # Insert "· Xplor" (not "· Chromium"): the broad grd rebrand replaces
    # Chromium->Xplor on the NEXT apply, which used to invalidate the marker and
    # make this edit stack a fresh prefix every run ("Xplor 0.8.10 · Xplor ...").
    _ver_marker = "Xplor " + XPLORER_VERSION + " · Xplor"
    if _ver_marker not in sst:
        # Bump-safe: matches a fresh checkout ("Version <ph>") OR any earlier
        # patched form ("Xplor 0.6.1 · Chromium/Xplor <ph>", possibly stacked)
        # and rewrites it to the current version.
        sst, _n = re.subn(
            r'(?:Xplor [0-9][0-9.]* · )*(?:Version|Xplor [0-9][0-9.]* · '
            r'(?:Chromium|Xplor)|Xplor) '
            r'(<ph name="PRODUCT_VERSION">)',
            _ver_marker + r" \1",
            sst, count=1)
        if _n:
            ss.write_text(sst)
            print(f"  edited (about version -> Xplorer {XPLORER_VERSION}): {ss}")

    # --- chrome/VERSION: monotonic PATCH so the Windows installer treats each
    # release as an UPGRADE. Upstream PATCH never changes (every build is
    # 151.0.7897.0), so setup.exe no-op'd a reinstall ("Higher version already
    # installed" / same-version repair) -> the installer appeared to do nothing.
    # Map XPLORER_VERSION "MAJ.MIN.PAT" -> PATCH = MIN*100 + PAT (0.8.6 -> 806),
    # well under the 16-bit VERSIONINFO / mac patch_hi-lo ceiling (65535).
    # chrome/VERSION is upstream (reverted by `git checkout -- .` each apply), so
    # the rewrite must live here, re-derived every apply.
    _xv = XPLORER_VERSION.split(".")
    _xpatch = int(_xv[1]) * 100 + int(_xv[2])
    version_file = src / "chrome/VERSION"
    vf = version_file.read_text()
    vf2 = re.sub(r"(?m)^PATCH=\d+$", f"PATCH={_xpatch}", vf)
    if vf2 != vf:
        version_file.write_text(vf2)
        print(f"  edited (chrome/VERSION PATCH -> {_xpatch}): {version_file}")

    # --- Windows install identity: "Chromium" -> "Xplorer" -------------------
    # Source of truth (chrome/install_static) for the install dir
    # (%LOCALAPPDATA%\<name>\Application), user-data dir, Software\<name> registry
    # root, Uninstall key, AppUserModelId, Default-Programs name, file-assoc
    # ProgIDs, and the elevation/tracing service NAMES. Without this, Xplorer
    # installed AS "Chromium" -> collided with real Chromium + shared its updater
    # identity (so reinstalls/updates targeted the same "Chromium"). install_static
    # is Windows-only (is_win in BUILD.gn) -> NEVER compiled on mac/linux, so a
    # malformed edit here is caught only by the Windows build, not the Mac pre-build.
    imh = src / "chrome/install_static/chromium_install_modes.h"
    t = imh.read_text()
    if 'kProductPathName[] = L"Xplorer"' not in t:
        t = t.replace('kProductPathName[] = L"Chromium";',
                      'kProductPathName[] = L"Xplorer";')
        t = t.replace('.base_app_name = L"Chromium",',
                      '.base_app_name = L"Xplorer",')
        t = t.replace('.base_app_id = L"Chromium",',
                      '.base_app_id = L"Xplorer",')
        t = t.replace('.browser_prog_id_prefix = L"ChromiumHTM",',
                      '.browser_prog_id_prefix = L"XplorerHTM",')
        t = t.replace('L"Chromium HTML Document",',
                      'L"Xplor HTML Document",')
        t = t.replace('.pdf_prog_id_prefix = L"ChromiumPDF",',
                      '.pdf_prog_id_prefix = L"XplorerPDF",')
        t = t.replace('L"Chromium PDF Document",',
                      'L"Xplor PDF Document",')
        # Active Setup GUID (system-level installs) -> fresh unique GUID.
        t = t.replace('L"{7D2B3E1D-D096-4594-9D8F-A6667F12E0AC}"',
                      'L"{63C1B345-8998-4A62-A654-70144D87282D}"')
        # Toast Activator CLSID -> fresh GUID 36DB671E-DF25-4F65-8C9B-963108D01396
        # (registered for USER installs too, so it MUST be unique vs Chromium's).
        t = re.sub(
            r"\.toast_activator_clsid = \{0x635EFA6F,.*?0x59\}\},",
            (".toast_activator_clsid = {0x36DB671E,\n"
             "                                  0xDF25,\n"
             "                                  0x4F65,\n"
             "                                  {0x8C, 0x9B, 0x96, 0x31, 0x08, 0xD0, 0x13,\n"
             "                                   0x96}},"),
            t, flags=re.DOTALL)
        imh.write_text(t)
        print(f"  edited (Windows install identity -> Xplorer): {imh}")

    # --- "Get help" links -> Xplorer GitHub (not Google support) -------------
    uc = src / "chrome/common/url_constants.h"
    uct = uc.read_text()
    if "github.com/daniel-farina/xplorer" not in uct:
        uct = uct.replace(
            '"https://support.google.com/chrome?p=help&ctx=settings"',
            '"https://github.com/daniel-farina/xplorer"')
        uct = uct.replace(
            '"https://support.google.com/chrome?p=help&ctx=menu"',
            '"https://github.com/daniel-farina/xplorer"')
        uc.write_text(uct)
        print(f"  edited (help -> GitHub): {uc}")

    # --- 3-dot menu + macOS menus: "About Chromium" -> "About Xplorer" -------
    cs = src / "chrome/app/chromium_strings.grd"
    cst = cs.read_text()
    if "About &amp;Xplor" not in cst:
        # All 3 non-"for Testing" IDS_ABOUT bodies (use_titlecase, not-
        # use_titlecase, is_chromeos). The "Google Chrome for Testing" lines
        # are a different string and are intentionally left alone.
        cst = cst.replace("About &amp;Chromium", "About &amp;Xplor")
        cs.write_text(cst)
        print(f"  edited (About Xplorer menus): {cs}")

    # macOS app-menu short name (IDS_APP_MENU_PRODUCT_NAME). Match the full
    # message tag so only this one "Chromium" body is touched.
    cst2 = cs.read_text()
    _app_menu_old = (
        '<message name="IDS_APP_MENU_PRODUCT_NAME" desc="The application\'s '
        "short name, used for the Mac's application menu, activity monitor, "
        "etc. This should be less than 16 characters. Example: Chrome, not "
        'Google Chrome." translateable="false">\n'
        "          Chromium\n"
        "        </message>"
    )
    _app_menu_new = (
        '<message name="IDS_APP_MENU_PRODUCT_NAME" desc="The application\'s '
        "short name, used for the Mac's application menu, activity monitor, "
        "etc. This should be less than 16 characters. Example: Chrome, not "
        'Google Chrome." translateable="false">\n'
        "          Xplor\n"
        "        </message>"
    )
    if _app_menu_new not in cst2 and _app_menu_old in cst2:
        cst2 = cst2.replace(_app_menu_old, _app_menu_new)
        cs.write_text(cst2)
        print(f"  edited (app-menu product name): {cs}")

    # macOS app menu "About Chromium" (IDS_ABOUT_MAC). Upstream uses a $1
    # substitution of the product name, which is unpatched and would render
    # "About Chromium". Pin it to a literal "About Xplor".
    gen = src / "chrome/app/generated_resources.grd"
    gent = gen.read_text()
    _about_mac_old = (
        '<message name="IDS_ABOUT_MAC" desc="The Mac menu item to open the '
        'about box.">\n'
        '          About <ph name="PRODUCT_NAME">$1<ex>Google Chrome</ex>'
        "</ph>\n"
        "        </message>"
    )
    _about_mac_new = (
        '<message name="IDS_ABOUT_MAC" desc="The Mac menu item to open the '
        'about box." translateable="false">\n'
        "          About Xplor\n"
        "        </message>"
    )
    if "About Xplor" not in gent and _about_mac_old in gent:
        gent = gent.replace(_about_mac_old, _about_mac_new)
        gen.write_text(gent)
        print(f"  edited (About Mac menu): {gen}")

    # --- About page license line: subject word only -------------------------
    # Keep the <ph>Chromium</ph> link text (correct attribution to the
    # upstream Chromium project); only swap the leading subject word.
    ccs = src / "components/components_chromium_strings.grd"
    ccst = ccs.read_text()
    if "Xplorer is made possible by the" not in ccst:
        ccst = ccst.replace(
            "Chromium is made possible by the",
            "Xplorer is made possible by the")
        ccs.write_text(ccst)
        print(f"  edited (license line): {ccs}")

    # macOS: Xplorer ships no Google updater, so the about page's update check
    # failed with "error code 0". Report up-to-date instead. (Live GitHub
    # release check deferred until the repo is public.)
    vum = src / "chrome/browser/ui/webui/help/version_updater_mac.mm"
    # macOS-only file: absent on Linux/Windows checkouts. Read as "" so the
    # anchored edits below all no-op (and never write) when it doesn't exist.
    vm = vum.read_text() if vum.exists() else ""
    _upd_pristine = (
        "  void CheckForUpdate(StatusCallback status_callback,\n"
        "                      PromoteCallback promote_callback) override {\n"
        "    updater::EnsureUpdater(\n"
        "        base::TaskPriority::USER_VISIBLE,\n"
        "        base::BindOnce(promote_callback, "
        "PromotionState::PROMOTE_ENABLED),\n"
        "        base::BindOnce(&updater::CheckForUpdate,\n"
        "                       base::BindRepeating(&UpdateStatus, "
        "status_callback)));\n"
        "  }")
    _upd_old = (
        "  void CheckForUpdate(StatusCallback status_callback,\n"
        "                      PromoteCallback promote_callback) override {\n"
        "    // XPLORER: Xplorer has no Google updater; skip the broken\n"
        "    // Keystone check (which reported \"error code 0\") and report\n"
        "    // up to date instead.\n"
        "    status_callback.Run(VersionUpdater::Status::UPDATED, 0, false,\n"
        "                        false, std::string(), 0, std::u16string());\n"
        "  }")
    # Live-check the Xplorer GitHub releases for a newer version (check-only;
    # no auto-download). Async NSURLSession; result posted back to the UI
    # sequence. "Update available" -> FAILED status carries a message+link.
    _upd_new = (
        "  void CheckForUpdate(StatusCallback status_callback,\n"
        "                      PromoteCallback promote_callback) override {\n"
        "    status_callback.Run(VersionUpdater::Status::CHECKING, 0, false,\n"
        "                        false, std::string(), 0, std::u16string());\n"
        "    scoped_refptr<base::SequencedTaskRunner> runner =\n"
        "        base::SequencedTaskRunner::GetCurrentDefault();\n"
        "    StatusCallback cb = status_callback;\n"
        "    NSURLSessionConfiguration* cfg =\n"
        "        [NSURLSessionConfiguration ephemeralSessionConfiguration];\n"
        "    cfg.timeoutIntervalForRequest = 10;\n"
        "    NSURLSession* session =\n"
        "        [NSURLSession sessionWithConfiguration:cfg];\n"
        "    NSURL* url = [NSURL URLWithString:\n"
        "        @\"https://api.github.com/repos/daniel-farina/xplorer/"
        "releases/latest\"];\n"
        "    NSURLSessionDataTask* task = [session dataTaskWithURL:url\n"
        "        completionHandler:^(NSData* data, NSURLResponse* response,\n"
        "                            NSError* error) {\n"
        "          std::string latest;\n"
        "          if (data && !error) {\n"
        "            NSDictionary* json = [NSJSONSerialization\n"
        "                JSONObjectWithData:data options:0 error:nil];\n"
        "            if ([json isKindOfClass:[NSDictionary class]]) {\n"
        "              NSString* tag = json[@\"tag_name\"];\n"
        "              if ([tag isKindOfClass:[NSString class]]) {\n"
        "                if ([tag hasPrefix:@\"v\"])\n"
        "                  tag = [tag substringFromIndex:1];\n"
        "                latest = base::SysNSStringToUTF8(tag);\n"
        "              }\n"
        "            }\n"
        "          }\n"
        "          VersionUpdater::Status status =\n"
        "              VersionUpdater::Status::UPDATED;\n"
        "          std::string out_version;\n"
        "          std::u16string message;\n"
        "          if (!latest.empty()) {\n"
        '            base::Version cur("' + XPLORER_VERSION + '");\n'
        "            base::Version newest(latest);\n"
        "            if (cur.IsValid() && newest.IsValid() &&\n"
        "                cur.CompareTo(newest) < 0) {\n"
        "              status = VersionUpdater::Status::FAILED;\n"
        "              out_version = latest;\n"
        "              message = base::UTF8ToUTF16(\n"
        '                  std::string("Xplor v") + latest +\n'
        '                  " is available -- download from "\n'
        '                  "github.com/daniel-farina/xplorer/releases/latest");\n'
        "            }\n"
        "          }\n"
        "          runner->PostTask(\n"
        "              FROM_HERE, base::BindOnce(cb, status, 0, false, false,\n"
        "                                        out_version, int64_t{0},\n"
        "                                        message));\n"
        "        }];\n"
        "    [task resume];\n"
        "  }")
    if vum.exists() and "releases/latest" not in vm:
        if _upd_pristine in vm:
            vm = vm.replace(_upd_pristine, _upd_new)
        elif _upd_old in vm:
            vm = vm.replace(_upd_old, _upd_new)
        if '#include "base/strings/sys_string_conversions.h"' not in vm:
            vm = vm.replace(
                '#include "base/version.h"\n',
                '#include "base/version.h"\n'
                '#include "base/location.h"\n'
                '#include "base/strings/sys_string_conversions.h"\n'
                '#include "base/task/sequenced_task_runner.h"\n')
        vum.write_text(vm)
        print(f"  edited (live update check): {vum}")
    # Our CheckForUpdate no longer calls the UpdateStatus helper, which now
    # trips -Werror,-Wunused-function. Mark it maybe_unused.
    vm2 = vum.read_text() if vum.exists() else ""
    if vum.exists() and "[[maybe_unused]] void UpdateStatus" not in vm2:
        vm2 = vm2.replace(
            "\nvoid UpdateStatus(VersionUpdater::StatusCallback",
            "\n[[maybe_unused]] void UpdateStatus(VersionUpdater::StatusCallback")
        vum.write_text(vm2)
        print(f"  edited (maybe_unused): {vum}")

    # About-page "Learn more" link (shown next to our "update available" status,
    # which we deliver as a FAILED status) pointed at Google's stock update-error
    # help page. Repoint it to the Xplorer releases page so it links to our
    # download, not Google. (The other two learn-more links in about_page.html are
    # obsolete-OS and branded-only macOS promote — not shown in our build.)
    about_page = src / "chrome/browser/resources/settings/about_page/about_page.html.ts"
    if about_page.exists():
        ap = about_page.read_text()
        if "github.com/daniel-farina/xplorer/releases/latest" not in ap:
            ap = ap.replace(
                'href="https://support.google.com/chrome?p=update_error"',
                'href="https://github.com/daniel-farina/xplorer/releases/latest"')
            about_page.write_text(ap)
            print(f"  edited (update learn-more URL): {about_page}")

    # Windows (unbranded, is_chrome_branded=false) compiles version_updater_basic
    # .cc, NOT version_updater_mac.mm. The basic updater reports DISABLED on the
    # About page; report up-to-date instead, matching the mac fix above. This is
    # best-effort: the exact upstream body varies across Chromium revisions, so
    # warn (don't fail the whole run) if the anchor isn't present — the basic
    # updater's DISABLED is cosmetic, not a hard error.
    vub = src / "chrome/browser/ui/webui/help/version_updater_basic.cc"
    if vub.exists():
        vb = vub.read_text()
        if "Run(UPDATED," in vb:
            print(f"  skip (already applied): {vub}")
        elif "Run(DISABLED," in vb:
            vb = vb.replace("Run(DISABLED,", "Run(UPDATED,", 1)  # XPLORER
            vub.write_text(vb)
            print(f"  edited: {vub}")
        else:
            print(f"  WARNING: {vub}: anchor 'Run(DISABLED,' not found; the "
                  "Windows About page may report 'updates disabled' (cosmetic)")

    # --- "Ask Google about this page" -> "Ask Grok about this page" ---------
    # Rebrand the Lens omnibox-action strings...
    omn = src / "components/omnibox_strings.grdp"
    og = omn.read_text()
    if "Ask Grok about this page" not in og:
        og = og.replace("Ask Google about this page", "Ask Grok about this page")
        og = og.replace("ask Google about this page", "ask Grok about this page")
        og = og.replace("Ask Google Lens about this page",
                        "Ask Grok about this page")
        og = og.replace("Ask Google Search about this page",
                        "Ask Grok about this page")
        omn.write_text(og)
        print(f"  edited: {omn}")
    # ...and rewire the action handler to open Grok instead of the Google Lens
    # overlay (the action routes 100% through OpenLensOverlay).
    acp = src / "chrome/browser/autocomplete/chrome_autocomplete_provider_client.cc"
    ac = acp.read_text()
    if "AskGrokAboutPage" not in ac:
        ac = ac.replace(
            '#include "chrome/browser/autocomplete/'
            'chrome_autocomplete_provider_client.h"',
            '#include "chrome/browser/autocomplete/'
            'chrome_autocomplete_provider_client.h"\n'
            '#include "chrome/browser/grok_companion/grok_companion_util.h"  '
            '// XPLORER', 1)
        ac = ac.replace(
            "void ChromeAutocompleteProviderClient::OpenLensOverlay(bool show) {\n"
            "#if !BUILDFLAG(IS_ANDROID)\n"
            "  if (auto* lens_search_controller =\n"
            "          GetLensSearchController(GetWebContents(web_contents_getter_))) {\n"
            "    if (show) {\n"
            "      // Force showing the contextual search box in the Lens Overlay.\n"
            "      lens_search_controller->OpenLensOverlay(\n"
            "          lens::LensOverlayInvocationSource::kOmniboxPageAction, true);\n"
            "    } else {\n"
            "      // TODO(crbug.com/402497756): For prototyping, reusing the existing\n"
            "      // omnibox entry point. However, for production, create a new invocation\n"
            "      // source for this new entry point.\n"
            "      lens_search_controller->StartContextualization(\n"
            "          lens::LensOverlayInvocationSource::kOmnibox);\n"
            "    }\n"
            "  }\n"
            "#endif  // !BUILDFLAG(IS_ANDROID)\n"
            "}",
            "void ChromeAutocompleteProviderClient::OpenLensOverlay(bool show) {\n"
            "  // XPLORER: \"Ask Grok about this page\" -> open Grok, not Google Lens.\n"
            "  grok_companion::AskGrokAboutPage(GetWebContents(web_contents_getter_));\n"
            "}")
        acp.write_text(ac)
        print(f"  edited: {acp}")

    # XPLORER: route Chrome's Google Lens "Search this tab with Image Search" and
    # the right-click "Search image with Google Lens" into Grok vision. Google Lens
    # is disabled in this fork; replace the LensSearchController overlay entry
    # points with grok_companion::GrokImageSearchForTab, which opens the Grok side
    # panel — the sidebar then screenshots the active tab and runs a Grok vision
    # analysis (grok-composer). Body-replaced via brace matching so the long method
    # bodies don't have to be reproduced verbatim.
    lsc = src / "chrome/browser/ui/lens/lens_search_controller.cc"
    lt = lsc.read_text()
    if "GrokImageSearchForTab" not in lt:
        anchor_inc = '#include "chrome/browser/ui/lens/lens_search_controller.h"'
        if anchor_inc not in lt:
            raise SystemExit("XPLORER: lens_search_controller.cc include anchor missing")
        lt = lt.replace(
            anchor_inc,
            anchor_inc +
            '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER'
            '\n#include "components/tabs/public/tab_interface.h"  // XPLORER', 1)
        grok_body = (
            "\n  // XPLORER: Google Lens image search is disabled; route to Grok "
            "vision.\n"
            "  grok_companion::GrokImageSearchForTab(\n"
            "      GetTabInterface() ? GetTabInterface()->GetBrowserWindowInterface()\n"
            "                        : nullptr);\n}")
        for sig in (
            "void LensSearchController::OpenLensOverlay(\n"
            "    lens::LensOverlayInvocationSource invocation_source,\n"
            "    bool should_show_csb) {",
            "void LensSearchController::OpenLensOverlayWithPendingRegion(\n"
            "    lens::LensOverlayInvocationSource invocation_source,\n"
            "    lens::mojom::CenterRotatedBoxPtr region,\n"
            "    const SkBitmap& region_bitmap) {",
        ):
            i = lt.find(sig)
            if i < 0:
                raise SystemExit("XPLORER: lens overlay signature not found: " + sig[:48])
            depth = 0
            j = i + len(sig) - 1  # the opening '{'
            while j < len(lt):
                if lt[j] == "{":
                    depth += 1
                elif lt[j] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            lt = lt[:i] + sig + grok_body + lt[j + 1:]
        lsc.write_text(lt)
        print(f"  edited: {lsc}")

    # Arc-style vertical sidebar: "Tabs" section label + agent tab group.
    patch_sidebar_space_menu(src)
    patch_folder_rows(src)
    patch_vertical_sidebar(src)
    patch_collapse_sidebar(src)
    patch_soft_tab_pills(src)
    patch_sidebar_plane(src)
    patch_hide_top_toolbar(src)
    patch_skip_crashed_session_bubble(src)
    patch_sidebar_location_focus(src)
    patch_enable_glass_frame(src)
    patch_quiet_new_tab_row(src)
    patch_hover_close(src)
    patch_quiet_tab_type(src)
    patch_quiet_sidebar_toolbar(src)
    patch_favorites_density(src)
    patch_quiet_toolbar_pins(src)
    patch_quiet_toolbar_edge(src)
    patch_toolbar_plane(src)
    patch_page_card(src)
    patch_quiet_toolbar_scale(src)
    patch_floating_address_pill(src)
    patch_hide_idle_reload(src)
    patch_quiet_omnibox_icon(src)
    patch_quiet_bookmark_star(src)

    patch_xplorer_settings_access(src)

    # Bundle the Grok companion UI into the Windows installer's version dir
    # (next to chrome.dll) so the gateway's UiDir() resolves it via DIR_MODULE on
    # installed builds; without it the installed app's /search (and other UI
    # routes) return the gateway's 401. The version dir is fully extracted by
    # setup (a ChromeDir-root subdir is not), so it must live there. Windows-only
    # (mini_installer); harmless on macOS, which doesn't read this file. The
    # build/packaging step stages companion/ui into the out dir to be picked up.
    chrome_release = src / "chrome/installer/mini_installer/chrome.release"
    if chrome_release.exists():
        edit(
            chrome_release,
            "chrome_proxy.exe: %(ChromeDir)s\\\n",
            "chrome_proxy.exe: %(ChromeDir)s\\\n"
            "# XPLORER: companion UI in the version dir (gateway UiDir/DIR_MODULE).\n"
            "companion\\ui\\*.*: %(VersionDir)s\\companion\\ui\\\n",
        )

    print("Integration edits applied.")


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "../chromium/src"))
