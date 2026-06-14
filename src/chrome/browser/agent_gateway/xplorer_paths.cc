// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/xplorer_paths.h"

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/values.h"

namespace xplorer_paths {
namespace {

constexpr char kGrokSettingsFile[] = "grok_settings.json";
constexpr char kLegacyDirs[][10] = {".xbrowser", ".aether"};

}  // namespace

base::FilePath DataDir() {
  static const base::NoDestructor<base::FilePath> cached([] {
    base::FilePath home;
    if (!base::PathService::Get(base::DIR_HOME, &home))
      return base::FilePath();
    base::FilePath xplorer = home.AppendASCII(kDataDir);
    if (!base::PathExists(xplorer)) {
      for (const char* legacy_name : kLegacyDirs) {
        base::FilePath legacy = home.AppendASCII(legacy_name);
        if (!base::PathExists(legacy))
          continue;
        if (!base::Move(legacy, xplorer))
          base::CopyDirectory(legacy, xplorer, true);
        break;
      }
      if (base::PathExists(xplorer)) {
        base::FilePath settings = xplorer.AppendASCII(kGrokSettingsFile);
        base::DictValue migrated;
        std::string json;
        if (base::ReadFileToString(settings, &json)) {
          if (auto parsed = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC))
            migrated = std::move(*parsed);
        }
        migrated.Set("welcome_completed", true);
        base::CreateDirectory(xplorer);
        if (base::JSONWriter::Write(migrated, &json))
          base::WriteFile(settings, json);
      }
    }
    base::CreateDirectory(xplorer);
    return xplorer;
  }());
  return *cached;
}

base::FilePath Resolve(const char* filename) {
  base::FilePath dir = DataDir();
  if (dir.empty())
    return base::FilePath();
  return dir.AppendASCII(filename);
}

}  // namespace xplorer_paths