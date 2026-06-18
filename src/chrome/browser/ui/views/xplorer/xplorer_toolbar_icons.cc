// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_icons.h"

#include "base/containers/fixed_flat_map.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/gfx/vector_icon_types.h"

namespace xplorer {

// Maps the toolbar.js icon vocabulary onto existing Chromium vector icons.
// Where no exact Material equivalent ships in this tree, the closest native
// glyph is used (e.g. "wrench"/"terminal" -> code, "brain" -> spark). Keeping
// the renderer on shipped icons avoids hand-authored .icon DSL entirely.
const gfx::VectorIcon& GetToolbarVectorIcon(std::string_view icon_id) {
  static constexpr auto kMap =
      base::MakeFixedFlatMap<std::string_view, const gfx::VectorIcon*>({
          // --- default-pill glyphs ---
          {"chat", &vector_icons::kChatIcon},
          {"wrench", &kCodeIcon},
          {"globe", &kGlobeIcon},
          {"image", &kImageIcon},
          {"book", &kMenuBookIcon},
          {"xmark", &kCloseIcon},
          {"sparkle", &kDraftSparkIcon},
          // --- catalog glyphs ---
          {"users", &kGroupCustomIcon},
          {"dollar", &vector_icons::kPaymentsIcon},
          {"chart", &kTableChartIcon},
          {"columns", &kTableChartIcon},
          {"mic", &vector_icons::kMicIcon},
          {"terminal", &kCodeIcon},
          {"code", &kCodeIcon},
          {"rocket", &kRocketLaunchIcon},
          {"briefcase", &vector_icons::kWorkIcon},
          {"home", &kHomeIcon},
          {"bell", &vector_icons::kNotificationsIcon},
          {"star", &kStarIcon},
          {"bolt", &kBoltIcon},
          {"brain", &kDraftSparkIcon},
          {"search", &vector_icons::kSearchIcon},
          {"gear", &kSettingsIcon},
          {"video", &vector_icons::kVideocamIcon},
          {"bookmark", &kStarIcon},
          {"compass", &kTravelExploreIcon},
          {"plus", &kAddIcon},
          {"news", &kMenuBookIcon},
          {"grid", &kGridViewIcon},
          {"grok", &kGrokIcon},
          {"link", &kLinkIcon},
          // dropdown affordance (no toolbar.js equivalent)
          {"caret", &kKeyboardArrowDownIcon},
      });
  const auto it = kMap.find(icon_id);
  return it != kMap.end() ? *it->second : kLinkIcon;
}

}  // namespace xplorer
