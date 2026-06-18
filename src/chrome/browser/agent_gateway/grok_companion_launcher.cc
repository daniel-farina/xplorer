// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/grok_companion_launcher.h"

#include "chrome/browser/agent_gateway/grok_native.h"
#include "chrome/browser/agent_gateway/xplorer_paths.h"

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"

namespace agent_gateway {

void WriteCompanionDiscovery(int gateway_port) {
  base::FilePath dir = xplorer_paths::DataDir();
  if (dir.empty())
    return;

  base::DictValue companion;
  companion.Set("url",
                "http://127.0.0.1:" + base::NumberToString(gateway_port));
  companion.Set("title", "Grok");
    companion.Set("model", "grok-composer-2.5-fast");
  companion.Set("native", true);
  base::FilePath grok_bin = ResolveGrokBinary();
  // FilePath::value() is std::wstring on Windows, so compare against a
  // FilePath literal and serialize via AsUTF8Unsafe() (JSON is UTF-8).
  if (!grok_bin.empty() &&
      grok_bin != base::FilePath(FILE_PATH_LITERAL("grok")))
    companion.Set("grok_bin", grok_bin.AsUTF8Unsafe());
  std::string json;
  if (base::JSONWriter::Write(companion, &json))
    base::WriteFile(dir.AppendASCII("companion.json"), json);

  base::FilePath gw = dir.AppendASCII("gateway.json");
  std::string gw_json;
  if (base::ReadFileToString(gw, &gw_json)) {
    if (auto parsed = base::JSONReader::ReadDict(gw_json, base::JSON_PARSE_RFC)) {
      parsed->Set("companion_url",
                    "http://127.0.0.1:" + base::NumberToString(gateway_port));
      std::string out;
      if (base::JSONWriter::Write(*parsed, &out))
        base::WriteFile(gw, out);
    }
  }
}

}  // namespace agent_gateway