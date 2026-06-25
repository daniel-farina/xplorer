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
  // The user/UI (no X-Agent-Id) may always activate.
  if (requester_agent_id.empty())
    return true;
  base::AutoLock guard(lock_);
  // An agent may activate only if it holds the current grant.
  return owner_ == requester_agent_id;
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
