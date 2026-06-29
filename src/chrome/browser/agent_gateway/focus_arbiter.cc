// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/focus_arbiter.h"

#include "base/no_destructor.h"

namespace agent_gateway {

// static
FocusArbiter* FocusArbiter::Get() {
  static base::NoDestructor<FocusArbiter> instance;
  return instance.get();
}

FocusArbiter::FocusArbiter() = default;
FocusArbiter::~FocusArbiter() = default;

bool FocusArbiter::MayActivate(const std::string& requester_agent_id) {
  base::AutoLock guard(lock_);
  // Default-deny: gateway-initiated foregrounding is allowed ONLY while a
  // user-granted focus is active (set via the user-only POST /focus). With no
  // grant, NO agent — chat, app-build, or browse, however many run at once —
  // can foreground a tab, so none can steal the user's focus or fight each
  // other. The user foregrounds tabs natively (the tab-strip click never hits
  // the gateway; the AgentTabGrouper observer keeps focus with the user) or via
  // the explicit "focus this task" grant. This does not rely on agents
  // self-identifying (they don't send X-Agent-Id), which is why the policy is
  // enforced server-side rather than per-agent-id.
  if (owner_.empty())
    return false;
  // Inside a user-granted window, allow the grant holder. Agents currently send
  // no X-Agent-Id, so an empty requester is permitted within the window; if a
  // session ever identifies itself it must match the grant holder.
  return requester_agent_id.empty() || requester_agent_id == owner_;
}

void FocusArbiter::SetOwner(const std::string& agent_id,
                            const std::string& grant_conv_id) {
  base::AutoLock guard(lock_);
  owner_ = agent_id;
  grant_conv_id_ = grant_conv_id;
}

void FocusArbiter::ResetToUser() {
  base::AutoLock guard(lock_);
  owner_.clear();
  grant_conv_id_.clear();
}

void FocusArbiter::OnRunEnded(const std::string& conv_id) {
  base::AutoLock guard(lock_);
  if (!conv_id.empty() && conv_id == grant_conv_id_) {
    owner_.clear();
    grant_conv_id_.clear();
  }
}

std::string FocusArbiter::owner() {
  base::AutoLock guard(lock_);
  return owner_;
}

}  // namespace agent_gateway
