// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_XPLORER_PATHS_H_
#define CHROME_BROWSER_AGENT_GATEWAY_XPLORER_PATHS_H_

#include "base/files/file_path.h"

namespace xplorer_paths {

inline constexpr char kProductName[] = "Xplorer";
inline constexpr char kDataDir[] = ".xplorer";

base::FilePath DataDir();
base::FilePath Resolve(const char* filename);

}  // namespace xplorer_paths

#endif  // CHROME_BROWSER_AGENT_GATEWAY_XPLORER_PATHS_H_