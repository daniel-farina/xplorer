// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/tab_ownership.h"

#include <memory>

#include "base/strings/string_util.h"
#include "content/public/browser/web_contents.h"

namespace agent_gateway {

namespace {
constexpr char kScheduledOwnerPrefix[] = "schedule:";
}  // namespace

bool IsScheduledTaskTab(const TabOwnership* own) {
  return own && !own->task_id.empty() &&
         base::StartsWith(own->owner, kScheduledOwnerPrefix);
}

// static
const void* const TabOwnership::kKey = &TabOwnership::kKey;

TabOwnership::TabOwnership() = default;
TabOwnership::~TabOwnership() = default;

// static
TabOwnership* TabOwnership::GetOrCreate(content::WebContents* wc) {
  TabOwnership* info = Get(wc);
  if (!info) {
    auto owned = std::make_unique<TabOwnership>();
    info = owned.get();
    wc->SetUserData(kKey, std::move(owned));
  }
  return info;
}

// static
TabOwnership* TabOwnership::Get(content::WebContents* wc) {
  return static_cast<TabOwnership*>(wc->GetUserData(kKey));
}

}  // namespace agent_gateway
