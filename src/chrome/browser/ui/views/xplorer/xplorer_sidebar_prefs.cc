// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_prefs.h"

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "chrome/browser/agent_gateway/xplorer_paths.h"

namespace xplorer {

namespace {

base::FilePath SettingsPath() {
  return xplorer_paths::Resolve("grok_settings.json");
}

base::DictValue LoadSettings() {
  const base::FilePath path = SettingsPath();
  if (path.empty()) {
    return base::DictValue();
  }
  std::string json;
  if (!base::ReadFileToString(path, &json)) {
    return base::DictValue();
  }
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  return parsed ? std::move(*parsed) : base::DictValue();
}

void SaveSettings(const base::DictValue& settings) {
  const base::FilePath path = SettingsPath();
  if (path.empty()) {
    return;
  }
  base::CreateDirectory(path.DirName());
  std::string json;
  if (base::JSONWriter::Write(settings, &json)) {
    base::WriteFile(path, json);
  }
}

}  // namespace

ToolbarPlacement GetToolbarPlacement() {
  const base::DictValue settings = LoadSettings();
  const std::string* placement =
      settings.FindStringByDottedPath("toolbar.placement");
  if (placement && base::EqualsCaseInsensitiveASCII(*placement, "top")) {
    return ToolbarPlacement::kTop;
  }
  return ToolbarPlacement::kSidebar;
}

void SetToolbarPlacement(ToolbarPlacement placement) {
  base::DictValue settings = LoadSettings();
  settings.EnsureDict("toolbar")->Set(
      "placement",
      placement == ToolbarPlacement::kTop ? "top" : "sidebar");
  SaveSettings(settings);
}

}  // namespace xplorer