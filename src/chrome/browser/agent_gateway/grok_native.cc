// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/grok_native.h"

#include <unistd.h>

#include <cstring>

#include <deque>
#include <map>
#include <set>
#include <utility>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/rand_util.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/agent_gateway/app_store.h"
#include "chrome/browser/agent_gateway/browser_api.h"
#include "chrome/browser/agent_gateway/xplorer_paths.h"
#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/browser/agent_gateway/tab_screenshot.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browser_task_traits.h"

#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"
#include "net/http/http_status_code.h"
#include "net/server/http_server_response_info.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"

namespace agent_gateway {
namespace {

constexpr char kGrokBin[] = "grok";

}  // namespace

base::FilePath ResolveGrokBinary() {
  static const base::NoDestructor<base::FilePath> cached([] {
    if (const char* env = getenv("GROK_BIN"); env && *env) {
      base::FilePath from_env(env);
      if (base::PathExists(from_env))
        return from_env;
    }
    base::FilePath home;
    if (base::PathService::Get(base::DIR_HOME, &home) && home.empty())
      home = base::FilePath();
    if (home.empty()) {
      if (const char* h = getenv("HOME"); h && *h)
        home = base::FilePath(h);
    }
    if (!home.empty()) {
      std::string companion_json;
      if (base::ReadFileToString(
              xplorer_paths::Resolve("companion.json"),
              &companion_json)) {
        if (auto parsed =
                base::JSONReader::ReadDict(companion_json, base::JSON_PARSE_RFC)) {
          if (const std::string* bin = parsed->FindString("grok_bin");
              bin && !bin->empty()) {
            base::FilePath from_cfg(*bin);
            if (base::PathExists(from_cfg))
              return from_cfg;
          }
        }
      }
      for (const char* rel : {".grok/bin/grok", ".local/bin/grok"}) {
        base::FilePath candidate = home.AppendASCII(rel);
        if (base::PathExists(candidate))
          return base::MakeAbsoluteFilePath(candidate);
      }
    }
    for (const char* abs :
         {"/opt/homebrew/bin/grok", "/usr/local/bin/grok"}) {
      base::FilePath candidate(abs);
      if (base::PathExists(candidate))
        return candidate;
    }
    return base::FilePath(kGrokBin);
  }());
  return *cached;
}

namespace {
// In-memory ring of recent gateway events, surfaced by GET /api/logs + /logs.
struct GatewayLogRing {
  base::Lock lock;
  std::deque<base::DictValue> events;
};
GatewayLogRing& LogRing() {
  static base::NoDestructor<GatewayLogRing> ring;
  return *ring;
}
constexpr size_t kMaxLogEvents = 500;
}  // namespace

void RecordGatewayLog(const std::string& level,
                      const std::string& source,
                      const std::string& app_id,
                      const std::string& event,
                      const std::string& message,
                      int exit_code,
                      const std::string& detail) {
  base::DictValue e;
  // Microseconds since the Windows epoch (same format as registry timestamps);
  // the UI converts to Unix ms. Uses only long-stable base APIs.
  e.Set("ts_us", static_cast<double>(
                     base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds()));
  e.Set("level", level);
  e.Set("source", source);
  e.Set("event", event);
  e.Set("message", message);
  if (!app_id.empty())
    e.Set("app_id", app_id);
  if (exit_code >= 0)
    e.Set("exit_code", exit_code);
  if (!detail.empty())
    e.Set("detail", detail);
  GatewayLogRing& ring = LogRing();
  base::AutoLock auto_lock(ring.lock);
  ring.events.push_back(std::move(e));
  while (ring.events.size() > kMaxLogEvents)
    ring.events.pop_front();
}

namespace {
// Snapshot the ring (newest last) applying optional source/app filters + limit.
base::ListValue SnapshotGatewayLogs(const std::string& source_filter,
                                    const std::string& app_filter,
                                    size_t limit) {
  base::ListValue out;
  GatewayLogRing& ring = LogRing();
  base::AutoLock auto_lock(ring.lock);
  for (const auto& e : ring.events) {
    if (!source_filter.empty()) {
      const std::string* s = e.FindString("source");
      if (!s || *s != source_filter)
        continue;
    }
    if (!app_filter.empty()) {
      const std::string* a = e.FindString("app_id");
      if (!a || *a != app_filter)
        continue;
    }
    out.Append(e.Clone());
  }
  while (limit > 0 && out.size() > limit)
    out.erase(out.begin());
  return out;
}

base::FilePath UiDir() {
  const char* env = getenv("XPLORER_COMPANION_UI");
  if (!env || !*env)
    env = getenv("XBROWSER_COMPANION_UI");
  if (env && *env)
    return base::FilePath(env);
  // Packaged app: the UI ships inside the bundle so the gateway is
  // self-contained on any machine. The gateway runs in the browser process, so
  // DIR_EXE is Xplorer.app/Contents/MacOS — the UI sits at ../Resources/...
  // Without this, a downloaded app has no UI on disk and every UI navigation
  // (e.g. /search) falls through to the auth-required path -> 401.
  base::FilePath exe_dir;
  if (base::PathService::Get(base::DIR_EXE, &exe_dir)) {
    base::FilePath bundled =
        exe_dir.DirName().AppendASCII("Resources").AppendASCII("companion").AppendASCII("ui");
    if (base::DirectoryExists(bundled))
      return bundled;
  }
  // Dev checkouts: serve straight from the repo / well-known home dirs.
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home))
    return base::FilePath();
  static constexpr const char* kCandidates[] = {
      "cli_experiment/xplorer/companion/ui",
      ".xplorer/companion/ui",
      ".xbrowser/companion/ui",
  };
  for (const char* rel : kCandidates) {
    base::FilePath candidate = home.AppendASCII(rel);
    if (base::DirectoryExists(candidate))
      return candidate;
  }
  return base::FilePath();
}

base::FilePath SessionsFile() {
  return xplorer_paths::Resolve("companion_sessions.json");
}

std::string PathOnly(const std::string& path) {
  return base::SplitString(path, "?", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY)[0];
}

std::map<std::string, std::string> QueryParams(const std::string& path) {
  std::map<std::string, std::string> params;
  auto parts = base::SplitString(path, "?", base::TRIM_WHITESPACE,
                                 base::SPLIT_WANT_NONEMPTY);
  if (parts.size() < 2)
    return params;
  for (const auto& pair :
       base::SplitString(parts[1], "&", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    auto kv = base::SplitString(pair, "=", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY);
    if (kv.size() == 2)
      params[kv[0]] = kv[1];
  }
  return params;
}

void SendBytes(net::HttpServer* server,
               int connection_id,
               net::HttpStatusCode code,
               std::string body,
               const char* content_type) {
  net::HttpServerResponseInfo resp(code);
  resp.SetBody(std::move(body), content_type);
  resp.AddHeader("Access-Control-Allow-Origin", "*");
  server->SendResponse(connection_id, resp, TRAFFIC_ANNOTATION_FOR_TESTS);
}

void SendJson(net::HttpServer* server,
              int connection_id,
              net::HttpStatusCode code,
              base::DictValue dict) {
  std::string json;
  base::JSONWriter::Write(dict, &json);
  SendBytes(server, connection_id, code, std::move(json), "application/json");
}

void SendJsonOnIO(net::HttpServer* server,
                  int connection_id,
                  net::HttpStatusCode code,
                  base::DictValue dict) {
  SendJson(server, connection_id, code, std::move(dict));
}

bool WantsHtml(const net::HttpServerRequestInfo& info) {
  auto it = info.headers.find("accept");
  if (it == info.headers.end())
    return false;
  return it->second.find("text/html") != std::string::npos;
}

bool ServeUiFile(net::HttpServer* server,
                 int connection_id,
                 const std::string& name) {
  base::FilePath dir = UiDir();
  if (dir.empty())
    return false;
  base::FilePath file = dir.AppendASCII(name);
  if (!base::PathExists(file))
    return false;
  std::string content;
  if (!base::ReadFileToString(file, &content))
    return false;
  const char* ctype = "text/html";
  if (base::EndsWith(name, ".css"))
    ctype = "text/css";
  else if (base::EndsWith(name, ".js"))
    ctype = "application/javascript";
  else if (base::EndsWith(name, ".ico"))
    ctype = "image/x-icon";
  else if (base::EndsWith(name, ".png"))
    ctype = "image/png";
  else if (base::EndsWith(name, ".svg"))
    ctype = "image/svg+xml";
  else if (base::EndsWith(name, ".jpg") || base::EndsWith(name, ".jpeg"))
    ctype = "image/jpeg";
  else if (base::EndsWith(name, ".webp"))
    ctype = "image/webp";
  SendBytes(server, connection_id, net::HTTP_OK, std::move(content), ctype);
  return true;
}

base::DictValue LoadSessions() {
  base::FilePath path = SessionsFile();
  if (path.empty())
    return base::DictValue();
  std::string json;
  if (!base::ReadFileToString(path, &json))
    return base::DictValue();
  auto parsed = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  return parsed ? std::move(*parsed) : base::DictValue();
}

void SaveSessions(const base::DictValue& data) {
  base::FilePath path = SessionsFile();
  if (path.empty())
    return;
  base::CreateDirectory(path.DirName());
  std::string json;
  if (base::JSONWriter::Write(data, &json))
    base::WriteFile(path, json);
}

constexpr char kDefaultModel[] = "grok-composer-2.5-fast";
constexpr char kSearchModel[] = "grok-build";
constexpr char kComposerModel[] = "grok-composer-2.5-fast";
constexpr char kSearchHomeBuild[] = "build";
constexpr char kSearchHomeWeb[] = "web";
constexpr char kSearchHomeWiki[] = "wiki";
constexpr char kGrokWikiHomeURL[] = "https://grokipedia.com/";

constexpr char kChatRules[] =
    "You are Grok, the native AI companion built into Xplorer. You can "
    "control the browser through MCP tools.";

constexpr char kBrowserChatRules[] =
    "You are Grok, the native AI companion built into Xplorer (Chromium). "
    "You MUST control the real browser through MCP tools — never give manual "
    "instructions the user must follow themselves. "
    "To organize tabs in one step, call xbrowser_organize_tabs (or "
    "POST /api/browser/organize-tabs on the companion). "
    "For custom grouping: xplorer_tabs then xbrowser_group_tabs. "
    "Use xbrowser_activate_tab / xbrowser_close_tab for focus and close. "
    "Bookmarks: xbrowser_bookmarks, xbrowser_add_bookmark, "
    "xbrowser_remove_bookmark. Navigation: xplorer_navigate or xplorer_new_tab.";

base::FilePath SettingsFile() {
  return xplorer_paths::Resolve("grok_settings.json");
}

base::DictValue LoadSettings() {
  base::FilePath path = SettingsFile();
  if (path.empty())
    return base::DictValue();
  std::string json;
  if (!base::ReadFileToString(path, &json))
    return base::DictValue();
  auto parsed = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  return parsed ? std::move(*parsed) : base::DictValue();
}

void SaveSettings(const base::DictValue& settings) {
  base::FilePath path = SettingsFile();
  if (path.empty())
    return;
  base::CreateDirectory(path.DirName());
  std::string json;
  if (base::JSONWriter::Write(settings, &json))
    base::WriteFile(path, json);
}

std::string GetConfiguredModel() {
  base::DictValue settings = LoadSettings();
  if (const std::string* model = settings.FindString("model");
      model && !model->empty()) {
    return *model;
  }
  return kDefaultModel;
}

void SetConfiguredModel(const std::string& model) {
  base::DictValue settings = LoadSettings();
  settings.Set("model", model);
  SaveSettings(settings);
}

std::string GetConfiguredSearchModel() {
  base::DictValue settings = LoadSettings();
  if (const std::string* model = settings.FindString("search_model");
      model && !model->empty()) {
    return *model;
  }
  return kSearchModel;
}

void SetConfiguredSearchModel(const std::string& model) {
  base::DictValue settings = LoadSettings();
  settings.Set("search_model", model);
  SaveSettings(settings);
}

std::string GetSearchHomeMode() {
  base::DictValue settings = LoadSettings();
  if (const std::string* mode = settings.FindString("search_home")) {
    if (*mode == kSearchHomeWeb)
      return kSearchHomeWeb;
    if (*mode == kSearchHomeWiki)
      return kSearchHomeWiki;
  }
  return kSearchHomeWeb;
}

void SetSearchHomeMode(const std::string& mode) {
  base::DictValue settings = LoadSettings();
  std::string saved = kSearchHomeBuild;
  if (mode == kSearchHomeWeb)
    saved = kSearchHomeWeb;
  else if (mode == kSearchHomeWiki)
    saved = kSearchHomeWiki;
  settings.Set("search_home", saved);
  SaveSettings(settings);
}

std::string ResolveModel(const std::string* request_model) {
  if (request_model && !request_model->empty())
    return *request_model;
  return GetConfiguredModel();
}

bool SearchModeNeedsWebTools(const std::string& mode) {
  return mode == "web" || mode == "videos";
}

// Composer has no web search; route web/video search through grok-build.
std::string ResolveSearchModel(const std::string& mode,
                               const std::string* request_model) {
  std::string model =
      request_model && !request_model->empty() ? *request_model
                                               : GetConfiguredSearchModel();
  if (SearchModeNeedsWebTools(mode) && model == kComposerModel)
    return kSearchModel;
  if (SearchModeNeedsWebTools(mode) && model == kDefaultModel)
    return kSearchModel;
  return model;
}

bool MessageNeedsBrowserTools(const std::string& message) {
  std::string lower = base::ToLowerASCII(message);
  static constexpr const char* kKeywords[] = {
      "tab",       "tabs",      "bookmark",  "bookmarks", "organize",
      "organise",  "group",     "browser",   "chrome",    "navigate",
      "close tab", "split tab", "history",   "xplorer",
  };
  for (const char* kw : kKeywords) {
    if (lower.find(kw) != std::string::npos)
      return true;
  }
  return false;
}

// Composer is fast for Q&A; browser control needs grok-build + MCP tools.
std::string ResolveChatModel(const std::string& message,
                             const std::string* request_model) {
  std::string model = ResolveModel(request_model);
  if (MessageNeedsBrowserTools(message) &&
      (model == kComposerModel || model == kDefaultModel)) {
    return kSearchModel;
  }
  return model;
}

const char* ChatRulesForMessage(const std::string& message) {
  if (MessageNeedsBrowserTools(message))
    return kBrowserChatRules;
  return kChatRules;
}

std::string ModelDisplayName(const std::string& model) {
  if (model == "grok-composer-2.5-fast")
    return "Composer 2.5";
  if (model == "grok-build")
    return "Grok Build";
  return model;
}

void EnrichSettingsResponse(base::DictValue* d, int gateway_port) {
  if (!d)
    return;
  d->Set("companion_url",
         "http://127.0.0.1:" + base::NumberToString(gateway_port));
  d->Set("search_model", GetConfiguredSearchModel());
  d->Set("search_model_label", ModelDisplayName(GetConfiguredSearchModel()));
  d->Set("grok_bin", ResolveGrokBinary().value());
  std::string gw_json;
  if (base::ReadFileToString(xplorer_paths::Resolve("gateway.json"),
                             &gw_json)) {
    if (auto parsed =
            base::JSONReader::ReadDict(gw_json, base::JSON_PARSE_RFC)) {
      if (const std::string* url = parsed->FindString("url"))
        d->Set("gateway_url", *url);
      if (const std::string* cdp = parsed->FindString("cdp_url"))
        d->Set("cdp_url", *cdp);
    }
  }
}

base::ListValue DefaultModelList() {
  base::ListValue models;
  for (const char* id :
       {"grok-composer-2.5-fast", "grok-build", "gemma-4-31b",
        "qwen3.6-35b-a3b"}) {
    base::DictValue m;
    m.Set("id", id);
    m.Set("label", ModelDisplayName(id));
    models.Append(std::move(m));
  }
  return models;
}

base::ListValue ListGrokModels() {
  base::CommandLine cmd(base::CommandLine::NO_PROGRAM);
  cmd.SetProgram(ResolveGrokBinary());
  cmd.AppendArg("models");
  std::string output;
  if (!base::GetAppOutput(cmd, &output) || output.empty())
    return DefaultModelList();

  base::ListValue models;
  for (const auto& line :
       base::SplitString(output, "\n", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    if (base::StartsWith(line, "Default model") ||
        base::StartsWith(line, "Available models") ||
        base::StartsWith(line, "You are logged in"))
      continue;
    if (line.find("grok-") == std::string::npos &&
        line.find("gemma-") == std::string::npos &&
        line.find("qwen") == std::string::npos)
      continue;
    std::string id = line;
    for (const char* prefix : {"* ", "- ", "  "}) {
      if (base::StartsWith(id, prefix))
        id = id.substr(strlen(prefix));
    }
    auto space = id.find(' ');
    if (space != std::string::npos)
      id = id.substr(0, space);
    if (id.empty() || id == "Default")
      continue;
    base::DictValue m;
    m.Set("id", id);
    m.Set("label", ModelDisplayName(id));
    models.Append(std::move(m));
  }
  return models.empty() ? DefaultModelList() : std::move(models);
}

struct SearchImageInput {
  std::string data;
  std::string mime = "image/png";
};

std::string TrimUrlTrailingPunct(std::string url) {
  while (!url.empty()) {
    const char c = url.back();
    if (c == ')' || c == ']' || c == '}' || c == '.' || c == ',' ||
        c == ';' || c == '"' || c == '\'' || c == '>' || c == '<')
      url.pop_back();
    else
      break;
  }
  return url;
}

bool IsHttpUrl(const std::string& url) {
  return base::StartsWith(url, "http://") || base::StartsWith(url, "https://");
}

std::string DetectProvider(const std::string& url) {
  if (url.find("youtube.com") != std::string::npos ||
      url.find("youtu.be") != std::string::npos)
    return "youtube";
  if (url.find("vimeo.com") != std::string::npos)
    return "vimeo";
  if (url.find("dailymotion.com") != std::string::npos)
    return "dailymotion";
  if (url.find("twitch.tv") != std::string::npos)
    return "twitch";
  return "";
}

std::string YouTubeVideoId(const std::string& url) {
  const std::string kWatch = "youtube.com/watch";
  auto vpos = url.find("v=");
  if (url.find(kWatch) != std::string::npos && vpos != std::string::npos) {
    size_t start = vpos + 2;
    size_t end = url.find_first_of("&?#", start);
    if (end == std::string::npos)
      end = url.size();
    return url.substr(start, end - start);
  }
  const std::string kShort = "youtu.be/";
  auto spos = url.find(kShort);
  if (spos != std::string::npos) {
    size_t start = spos + kShort.size();
    size_t end = url.find_first_of("?#&/", start);
    if (end == std::string::npos)
      end = url.size();
    return url.substr(start, end - start);
  }
  return "";
}

std::string VideoThumbnailForUrl(const std::string& url) {
  const std::string provider = DetectProvider(url);
  if (provider == "youtube") {
    const std::string id = YouTubeVideoId(url);
    if (!id.empty())
      return "https://img.youtube.com/vi/" + id + "/mqdefault.jpg";
  }
  return "";
}

void EnrichMediaItem(base::DictValue* item) {
  if (!item)
    return;
  const std::string* url = item->FindString("url");
  if (!url || url->empty())
    return;
  if (!item->FindString("provider")) {
    const std::string provider = DetectProvider(*url);
    if (!provider.empty())
      item->Set("provider", provider);
  }
  if (!item->FindString("thumbnail")) {
    const std::string thumb = VideoThumbnailForUrl(*url);
    if (!thumb.empty())
      item->Set("thumbnail", thumb);
  }
}

void AddUniqueLink(const std::string& url,
                   const std::string& title,
                   const std::string& snippet,
                   const std::string& kind,
                   base::ListValue* out,
                   std::set<std::string>* seen) {
  std::string clean = TrimUrlTrailingPunct(url);
  if (!IsHttpUrl(clean) || seen->count(clean))
    return;
  seen->insert(clean);
  base::DictValue item;
  item.Set("url", clean);
  item.Set("title", title.empty() ? clean : title);
  if (!snippet.empty())
    item.Set("snippet", snippet);
  if (!kind.empty())
    item.Set("kind", kind);
  EnrichMediaItem(&item);
  out->Append(std::move(item));
}

std::string TitleBeforeUrl(const std::string& text, size_t url_pos) {
  size_t line_start = text.rfind('\n', url_pos);
  if (line_start == std::string::npos)
    line_start = 0;
  else
    ++line_start;
  const std::string line = text.substr(line_start, url_pos - line_start);
  size_t bold = line.find("**");
  if (bold != std::string::npos) {
    size_t bold_end = line.find("**", bold + 2);
    if (bold_end != std::string::npos)
      return line.substr(bold + 2, bold_end - bold - 2);
  }
  size_t bracket = line.rfind("](", url_pos - line_start);
  if (bracket != std::string::npos && bracket > 0 && line[bracket - 1] == ']') {
    size_t open = line.rfind('[', bracket - 1);
    if (open != std::string::npos && open + 1 < bracket)
      return line.substr(open + 1, bracket - open - 1);
  }
  return "";
}

void ExtractUrlsFromText(const std::string& text,
                         const std::string& kind,
                         base::ListValue* out,
                         std::set<std::string>* seen) {
  for (const char* scheme : {"https://", "http://"}) {
    size_t pos = 0;
    while ((pos = text.find(scheme, pos)) != std::string::npos) {
      size_t end = text.find_first_of(" \n\r\t\"')<>", pos);
      if (end == std::string::npos)
        end = text.size();
      const std::string url = text.substr(pos, end - pos);
      const std::string title = TitleBeforeUrl(text, pos);
      AddUniqueLink(url, title, "", kind, out, seen);
      pos = end;
    }
  }
}

std::string ExtractJsonPayload(const std::string& text) {
  size_t fence = text.find("```json");
  if (fence != std::string::npos) {
    size_t start = text.find('\n', fence);
    if (start != std::string::npos) {
      ++start;
      const size_t end = text.find("```", start);
      if (end != std::string::npos)
        return text.substr(start, end - start);
    }
  }
  for (const char* key :
       {"{\"videos\"", "{\"links\"", "{\"images\"", "{\"answer\""}) {
    size_t pos = text.rfind(key);
    if (pos != std::string::npos)
      return text.substr(pos);
  }
  const size_t pos = text.rfind("\n{");
  if (pos != std::string::npos)
    return text.substr(pos + 1);
  return "";
}

void AppendParsedItems(const base::ListValue* src,
                       const std::string& kind,
                       base::ListValue* links,
                       base::ListValue* images,
                       base::ListValue* videos,
                       std::set<std::string>* seen) {
  if (!src)
    return;
  for (const auto& entry : *src) {
    if (!entry.is_dict())
      continue;
    base::DictValue item = entry.GetDict().Clone();
    const std::string* url = item.FindString("url");
    if (!url || url->empty())
      continue;
    std::string clean = TrimUrlTrailingPunct(*url);
    if (!IsHttpUrl(clean) || seen->count(clean))
      continue;
    seen->insert(clean);
    item.Set("url", clean);
    if (!item.FindString("kind"))
      item.Set("kind", kind);
    EnrichMediaItem(&item);
    if (kind == "video" && videos)
      videos->Append(item.Clone());
    else if (kind == "image" && images)
      images->Append(item.Clone());
    else if (links)
      links->Append(std::move(item));
  }
}

const char* SearchPromptForMode(const std::string& mode, bool has_image) {
  if (mode == "images" && has_image) {
    return "You are Grok Vision Search for Xplorer. Analyze the attached "
           "image; use web search for similar images. Give a short answer, "
           "then end with ONLY a ```json code block:\n"
           "{\"answer\":\"...\",\"images\":[{\"title\":\"...\",\"url\":"
           "\"https://...\",\"thumbnail\":\"https://...\",\"source\":"
           "\"...\"}],\"links\":[{\"title\":\"...\",\"url\":\"https://..."
           ",\"snippet\":\"...\"}]}\n"
           "Include up to 20 image results.";
  }
  if (mode == "images") {
    return "You are Grok Image Search for Xplorer. Use web search for image "
           "results across multiple sites. Short summary, then ONLY ```json:\n"
           "{\"answer\":\"...\",\"images\":[{\"title\":\"...\",\"url\":"
           "\"https://...\",\"thumbnail\":\"https://...\",\"source\":"
           "\"...\"}],\"links\":[{\"title\":\"...\",\"url\":\"https://..."
           ",\"snippet\":\"...\"}]}\n"
           "Include up to 20 images.";
  }
  if (mode == "videos") {
    return "You are Grok Video Search for Xplorer. Use web search. Find videos "
           "on YouTube, Vimeo, and Dailymotion. Brief summary, then ONLY "
           "```json:\n"
           "{\"videos\":[{\"title\":\"...\",\"url\":\"https://...\","
           "\"provider\":\"youtube|vimeo|dailymotion\",\"snippet\":\"...\"}]}"
           "\n"
           "Include 12-20 videos from multiple providers when available.";
  }
  if (mode == "imagine") {
    return "Generate an image for this prompt. Return a brief caption then "
           "any image URLs produced.";
  }
  return "You are Grok Search for Xplorer. Use web search. Give a concise "
         "formatted answer with citations, then ONLY ```json:\n"
         "{\"links\":[{\"title\":\"...\",\"url\":\"https://...\","
         "\"snippet\":\"...\"}]}\n"
         "Include up to 20 diverse, high-quality sources.";
}

base::CommandLine BuildGrokSearchCommand(const std::string& query,
                                         const std::string& mode,
                                         const std::string& model,
                                         bool streaming,
                                         const SearchImageInput* image) {
  const bool has_image = image && !image->data.empty();
  std::string user_query =
      query.empty() ? "Describe this image and find similar images online."
                    : query;
  std::string prompt = std::string(SearchPromptForMode(mode, has_image)) +
                       "\n\nQuery: " + user_query;
  base::CommandLine cmd(base::CommandLine::NO_PROGRAM);
  cmd.SetProgram(ResolveGrokBinary());
  if (has_image) {
    base::ListValue blocks;
    base::DictValue img;
    img.Set("type", "image");
    img.Set("data", image->data);
    img.Set("mimeType", image->mime.empty() ? "image/png" : image->mime);
    blocks.Append(std::move(img));
    base::DictValue txt;
    txt.Set("type", "text");
    txt.Set("text", prompt);
    blocks.Append(std::move(txt));
    std::string json;
    base::JSONWriter::Write(blocks, &json);
    cmd.AppendArg("--prompt-json");
    cmd.AppendArg(json);
  } else {
    cmd.AppendArg("-p");
    cmd.AppendArg(prompt);
  }
  cmd.AppendArg("--output-format");
  cmd.AppendArg(streaming ? "streaming-json" : "json");
  cmd.AppendArg("--always-approve");
  cmd.AppendArg("-m");
  cmd.AppendArg(model);
  cmd.AppendArg("--max-turns");
  cmd.AppendArg(has_image ? "20" : "15");
  if (mode == "imagine")
    cmd.AppendArg("--disable-web-search");
  return cmd;
}

base::DictValue ParseSearchText(const std::string& text,
                               const std::string& mode) {
  base::ListValue links;
  base::ListValue images;
  base::ListValue videos;
  std::set<std::string> seen;

  std::string answer = text;
  const std::string json_blob = ExtractJsonPayload(text);
  if (!json_blob.empty()) {
    size_t json_start = text.find(json_blob);
    if (json_start != std::string::npos) {
      answer = std::string(base::TrimWhitespaceASCII(
          text.substr(0, json_start), base::TRIM_TRAILING));
      while (base::EndsWith(answer, "`"))
        answer.pop_back();
      answer = std::string(
          base::TrimWhitespaceASCII(answer, base::TRIM_TRAILING));
      if (base::EndsWith(answer, "```json"))
        answer.resize(answer.size() - 7);
      else if (base::EndsWith(answer, "```"))
        answer.resize(answer.size() - 3);
      answer = std::string(
          base::TrimWhitespaceASCII(answer, base::TRIM_TRAILING));
    }
    if (auto link_json =
            base::JSONReader::ReadDict(json_blob, base::JSON_PARSE_RFC)) {
      AppendParsedItems(link_json->FindList("videos"), "video", &links,
                        &images, &videos, &seen);
      AppendParsedItems(link_json->FindList("images"), "image", &links,
                        &images, &videos, &seen);
      AppendParsedItems(link_json->FindList("links"), "link", &links, &images,
                        &videos, &seen);
    }
  }

  const std::string kind =
      mode == "videos" ? "video" : mode == "images" ? "image" : "link";
  if (links.empty() && videos.empty())
    ExtractUrlsFromText(text, kind, &links, &seen);
  if (mode == "videos" && videos.empty()) {
    for (const auto& entry : links) {
      if (entry.is_dict())
        videos.Append(entry.GetDict().Clone());
    }
  }

  base::DictValue result;
  result.Set("mode", mode);
  result.Set("answer", answer);
  result.Set("text", answer);
  result.Set("links", std::move(links));
  if (!videos.empty())
    result.Set("videos", std::move(videos));
  if (!images.empty())
    result.Set("images", std::move(images));
  if (mode == "imagine" && images.empty()) {
    base::ListValue url_images;
    size_t pos = 0;
    while ((pos = text.find("http", pos)) != std::string::npos) {
      size_t end = text.find_first_of(" \n\r\t\"')", pos);
      if (end == std::string::npos)
        end = text.size();
      std::string url = text.substr(pos, end - pos);
      if (url.find(".png") != std::string::npos ||
          url.find(".jpg") != std::string::npos ||
          url.find(".webp") != std::string::npos) {
        url_images.Append(url);
      }
      pos = end;
    }
    if (!url_images.empty())
      result.Set("images", std::move(url_images));
  }
  return result;
}

void SendHttpChunk(net::HttpServer* server,
                   int connection_id,
                   const std::string& data) {
  if (data.empty())
    return;
  std::string frame =
      base::StringPrintf("%zx\r\n", data.size());
  frame.append(data);
  frame.append("\r\n");
  server->SendRaw(connection_id, frame, TRAFFIC_ANNOTATION_FOR_TESTS);
}

void SendStreamItem(net::HttpServer* server,
                    int connection_id,
                    base::DictValue item) {
  item.Set("type", "item");
  std::string line;
  base::JSONWriter::Write(item, &line);
  line.push_back('\n');
  SendHttpChunk(server, connection_id, line);
}

void EmitNewItemsFromText(net::HttpServer* server,
                          int connection_id,
                          const std::string& text,
                          const std::string& mode,
                          std::set<std::string>* seen) {
  base::ListValue batch;
  const std::string kind =
      mode == "videos" ? "video" : mode == "images" ? "image" : "link";
  ExtractUrlsFromText(text, kind, &batch, seen);
  for (const auto& entry : batch) {
    if (entry.is_dict())
      SendStreamItem(server, connection_id, entry.GetDict().Clone());
  }
}

void BeginNdjsonStream(net::HttpServer* server, int connection_id) {
  server->SendRaw(
      connection_id,
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/x-ndjson\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Cache-Control: no-cache\r\n"
      "Connection: keep-alive\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "\r\n",
      TRAFFIC_ANNOTATION_FOR_TESTS);
}

void SendStreamMeta(net::HttpServer* server,
                    int connection_id,
                    const std::string& model,
                    const std::string& mode) {
  base::DictValue meta;
  meta.Set("type", "meta");
  meta.Set("model", model);
  meta.Set("model_label", ModelDisplayName(model));
  if (!mode.empty())
    meta.Set("mode", mode);
  std::string line;
  base::JSONWriter::Write(meta, &line);
  line.push_back('\n');
  SendHttpChunk(server, connection_id, line);
}

void BeginNdjsonStreamWithMeta(net::HttpServer* server,
                               int connection_id,
                               const std::string& model,
                               const std::string& mode) {
  BeginNdjsonStream(server, connection_id);
  SendStreamMeta(server, connection_id, model, mode);
}

void EndNdjsonStream(net::HttpServer* server, int connection_id) {
  server->SendRaw(connection_id, "0\r\n\r\n", TRAFFIC_ANNOTATION_FOR_TESTS);
}

std::string StripAnsiEscapes(std::string s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
      i += 2;
      while (i < s.size() && s[i] != 'm')
        ++i;
      continue;
    }
    out.push_back(s[i]);
  }
  return out;
}

void SendStreamError(net::HttpServer* server,
                     int connection_id,
                     const std::string& message) {
  base::DictValue err;
  err.Set("type", "error");
  err.Set("error", message);
  std::string line;
  base::JSONWriter::Write(err, &line);
  line.push_back('\n');
  BeginNdjsonStream(server, connection_id);
  SendHttpChunk(server, connection_id, line);
  EndNdjsonStream(server, connection_id);
}

enum class GrokStreamKind { kSearch, kChat, kAppBuild };

void SaveChatAssistantReply(const std::string& conv_id,
                            const std::string& text,
                            const std::string& session_id) {
  base::DictValue data = LoadSessions();
  base::ListValue* convs = data.FindList("conversations");
  if (!convs)
    return;
  for (auto& v : *convs) {
    if (!v.is_dict())
      continue;
    const std::string* id = v.GetDict().FindString("id");
    if (!id || *id != conv_id)
      continue;
    if (!session_id.empty())
      v.GetDict().Set("session_id", session_id);
    base::DictValue assistant;
    assistant.Set("role", "assistant");
    assistant.Set("content", text);
    if (base::ListValue* msgs = v.GetDict().FindList("messages"))
      msgs->Append(std::move(assistant));
    break;
  }
  SaveSessions(data);
}

bool MessageWantsOrganizeTabs(const std::string& message) {
  std::string lower = base::ToLowerASCII(message);
  if (lower.find("tab") == std::string::npos)
    return false;
  return lower.find("organiz") != std::string::npos ||
         lower.find("organis") != std::string::npos ||
         (lower.find("group") != std::string::npos);
}

std::string FormatOrganizeTabsReply(const base::DictValue& result) {
  if (const std::string* err = result.FindString("error"))
    return std::string("Could not organize tabs: ") + *err;
  std::string reply = "**Tabs organized** into native Chrome groups:\n\n";
  if (const base::ListValue* groups = result.FindList("groups")) {
    for (const auto& v : *groups) {
      if (!v.is_dict())
        continue;
      const std::string* title = v.GetDict().FindString("title");
      size_t count = 0;
      if (const base::ListValue* ids = v.GetDict().FindList("tab_ids"))
        count = ids->size();
      reply += "- **";
      reply += title && !title->empty() ? *title : "Group";
      reply += "** (";
      reply += base::NumberToString(count);
      reply += " tabs)\n";
    }
  }
  if (const std::optional<int> n = result.FindInt("tabs")) {
    reply += "\n";
    reply += base::NumberToString(*n);
    reply += " tabs total.";
  }
  return reply;
}

void RunOrganizeTabsFastPath(
    net::HttpServer* server,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    int connection_id,
    std::string conv_id) {
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](net::HttpServer* srv,
             scoped_refptr<base::SingleThreadTaskRunner> io, int cid,
             std::string conv_id) {
            BrowserApi::OrganizeTabs(base::BindOnce(
                [](net::HttpServer* srv,
                   scoped_refptr<base::SingleThreadTaskRunner> io, int cid,
                   std::string conv_id, base::DictValue result) {
                  std::string reply = FormatOrganizeTabsReply(result);
                  if (!conv_id.empty() && !reply.empty())
                    SaveChatAssistantReply(conv_id, reply, "");
                  io->PostTask(
                      FROM_HERE,
                      base::BindOnce(
                          [](net::HttpServer* srv, int cid,
                             std::string reply) {
                            BeginNdjsonStreamWithMeta(srv, cid, "native",
                                                      "chat");
                            base::DictValue text_evt;
                            text_evt.Set("type", "text");
                            text_evt.Set("data", reply);
                            std::string line;
                            base::JSONWriter::Write(text_evt, &line);
                            line.push_back('\n');
                            SendHttpChunk(srv, cid, std::move(line));
                            base::DictValue done;
                            done.Set("type", "result");
                            done.Set("reply", reply);
                            done.Set("text", reply);
                            done.Set("model", "native");
                            done.Set("model_label", "Xplorer");
                            std::string done_line;
                            base::JSONWriter::Write(done, &done_line);
                            done_line.push_back('\n');
                            SendHttpChunk(srv, cid, std::move(done_line));
                            EndNdjsonStream(srv, cid);
                          },
                          srv, cid, std::move(reply)));
                },
                srv, io, cid, conv_id));
          },
          server, io_task_runner, connection_id, std::move(conv_id)));
}

void PumpGrokStream(net::HttpServer* server,
                    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
                    int connection_id,
                    base::CommandLine cmd,
                    std::string model,
                    std::string mode,
                    GrokStreamKind kind,
                    std::string conv_id,
                    std::string app_id) {
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    io_task_runner->PostTask(
        FROM_HERE, base::BindOnce(&SendStreamError, server, connection_id,
                                  "failed to create pipe"));
    return;
  }

  const std::string log_source =
      kind == GrokStreamKind::kAppBuild
          ? "build"
          : (kind == GrokStreamKind::kSearch ? "search" : "chat");

  base::LaunchOptions options;
  // Only stream stdout (streaming-json). Merging stderr injects ANSI logs like
  // "[2m2026-..." that break NDJSON parsing in the companion UI. Capture stderr
  // to a temp file instead so the real failure reason (otherwise discarded) is
  // recoverable for the error event + /api/logs.
  options.fds_to_remap.emplace_back(pipe_fds[1], STDOUT_FILENO);
  base::FilePath stderr_path;
  base::File stderr_file;
  if (base::CreateTemporaryFile(&stderr_path)) {
    stderr_file.Initialize(
        stderr_path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  }
  if (stderr_file.IsValid()) {
    options.fds_to_remap.emplace_back(stderr_file.GetPlatformFile(),
                                      STDERR_FILENO);
  }
  base::Process process = base::LaunchProcess(cmd, options);
  close(pipe_fds[1]);
  stderr_file.Close();  // child holds its own copy; we read by path on exit.

  if (!process.IsValid()) {
    close(pipe_fds[0]);
    if (!stderr_path.empty())
      base::DeleteFile(stderr_path);
    std::string launch_msg =
        "failed to launch grok at " + cmd.GetProgram().MaybeAsASCII();
    RecordGatewayLog("error", log_source, app_id, "launch_fail", launch_msg, -1,
                     "");
    io_task_runner->PostTask(
        FROM_HERE,
        base::BindOnce(
            &SendStreamError, server, connection_id,
            launch_msg +
                " — run `grok login` or set GROK_BIN in ~/.xplorer/companion.json"));
    return;
  }

  RecordGatewayLog("info", log_source, app_id, "start",
                   log_source == "build" ? "app build started"
                                         : "grok run started",
                   -1, "");

  io_task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&BeginNdjsonStreamWithMeta, server, connection_id, model,
                     kind == GrokStreamKind::kSearch ? mode : "chat"));

  base::File read_file(pipe_fds[0]);
  std::string buffer;
  std::string full_text;
  std::string session_id;
  std::set<std::string> emitted_urls;
  char read_buf[4096];
  while (true) {
    int n = HANDLE_EINTR(
        read(read_file.GetPlatformFile(), read_buf, sizeof(read_buf)));
    if (n <= 0)
      break;
    buffer.append(read_buf, n);
    size_t newline = std::string::npos;
    while ((newline = buffer.find('\n')) != std::string::npos) {
      std::string line =
          StripAnsiEscapes(buffer.substr(0, newline));
      buffer.erase(0, newline + 1);
      base::TrimWhitespaceASCII(line, base::TRIM_ALL, &line);
      if (line.empty() || line[0] != '{')
        continue;
      if (auto parsed =
              base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC)) {
        const std::string* type = parsed->FindString("type");
        if (type && *type == "text") {
          if (const std::string* data = parsed->FindString("data")) {
            full_text += *data;
            if (kind == GrokStreamKind::kSearch) {
              EmitNewItemsFromText(server, connection_id, full_text, mode,
                                   &emitted_urls);
            }
          }
        } else if (type && *type == "end") {
          if (const std::string* sid = parsed->FindString("sessionId"))
            session_id = *sid;
        }
      }
      std::string forward = line + "\n";
      io_task_runner->PostTask(
          FROM_HERE, base::BindOnce(&SendHttpChunk, server, connection_id,
                                    std::move(forward)));
    }
  }

  int exit_code = -1;
  process.WaitForExit(&exit_code);

  // Recover the otherwise-discarded stderr so failures show a real reason.
  std::string stderr_tail;
  if (!stderr_path.empty()) {
    std::string stderr_all;
    if (base::ReadFileToString(stderr_path, &stderr_all)) {
      stderr_all = StripAnsiEscapes(stderr_all);
      base::TrimWhitespaceASCII(stderr_all, base::TRIM_ALL, &stderr_all);
      stderr_tail = stderr_all.size() > 4096
                        ? stderr_all.substr(stderr_all.size() - 4096)
                        : stderr_all;
    }
    base::DeleteFile(stderr_path);
  }

  RecordGatewayLog(
      exit_code == 0 ? "info" : "error", log_source, app_id,
      exit_code == 0 ? "finish" : "failed",
      exit_code == 0
          ? (log_source == "build" ? "app build finished" : "grok run finished")
          : ("grok failed (exit " + base::NumberToString(exit_code) + ")"),
      exit_code, exit_code == 0 ? std::string() : stderr_tail);

  if (exit_code != 0) {
    if (!app_id.empty()) {
      OnAppBuildStreamFinished(app_id, conv_id, exit_code, session_id,
                               full_text, stderr_tail);
    }
    base::DictValue err;
    err.Set("type", "error");
    std::string err_msg =
        "grok failed (exit " + base::NumberToString(exit_code) + ")";
    if (!stderr_tail.empty())
      err_msg += ": " + stderr_tail;
    err.Set("error", err_msg);
    std::string err_line;
    base::JSONWriter::Write(err, &err_line);
    err_line.push_back('\n');
    io_task_runner->PostTask(
        FROM_HERE, base::BindOnce(&SendHttpChunk, server, connection_id,
                                  std::move(err_line)));
    io_task_runner->PostTask(
        FROM_HERE,
        base::BindOnce(&EndNdjsonStream, server, connection_id));
    return;
  }

  base::DictValue result_event;
  result_event.Set("type", "result");
  result_event.Set("model", model);
  result_event.Set("model_label", ModelDisplayName(model));
  if (kind == GrokStreamKind::kSearch) {
    base::DictValue parsed = ParseSearchText(full_text, mode);
    if (const std::string* answer = parsed.FindString("answer"))
      result_event.Set("answer", *answer);
    if (const std::string* text = parsed.FindString("text"))
      result_event.Set("text", *text);
    result_event.Set("mode", mode);
    if (const base::ListValue* links = parsed.FindList("links"))
      result_event.Set("links", links->Clone());
    if (const base::ListValue* videos = parsed.FindList("videos"))
      result_event.Set("videos", videos->Clone());
    if (const base::ListValue* images = parsed.FindList("images"))
      result_event.Set("images", images->Clone());
  } else {
    result_event.Set("text", full_text);
    result_event.Set("reply", full_text);
    if (!session_id.empty())
      result_event.Set("sessionId", session_id);
    if (!conv_id.empty() && !full_text.empty())
      SaveChatAssistantReply(conv_id, full_text, session_id);
    if (!app_id.empty())
      OnAppBuildStreamFinished(app_id, conv_id, exit_code, session_id,
                               full_text, stderr_tail);
  }
  std::string result_line;
  base::JSONWriter::Write(result_event, &result_line);
  result_line.push_back('\n');

  io_task_runner->PostTask(
      FROM_HERE, base::BindOnce(&SendHttpChunk, server, connection_id,
                                std::move(result_line)));
  io_task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&EndNdjsonStream, server, connection_id));
}

void RunGrokSearchStream(
    net::HttpServer* server,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    int connection_id,
    std::string query,
    std::string mode,
    std::string model,
    SearchImageInput image) {
  base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE})
      ->PostTask(FROM_HERE,
                 base::BindOnce(
                     &PumpGrokStream, server, io_task_runner, connection_id,
                     BuildGrokSearchCommand(query, mode, model, true,
                                            image.data.empty() ? nullptr
                                                               : &image),
                     model, mode, GrokStreamKind::kSearch, "", ""));
}

content::WebContents* FindWebContentsByTabId(const std::string& tab_id) {
  auto parts = base::SplitString(tab_id, ":", base::TRIM_WHITESPACE,
                                 base::SPLIT_WANT_NONEMPTY);
  if (parts.size() != 2)
    return nullptr;
  int sid = 0, index = 0;
  if (!base::StringToInt(parts[0], &sid) || !base::StringToInt(parts[1], &index))
    return nullptr;
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser->GetSessionID().id() != sid)
      continue;
    TabStripModel* model = browser->GetTabStripModel();
    if (index >= 0 && index < model->count())
      return model->GetWebContentsAt(index);
  }
  return nullptr;
}

content::WebContents* FindActiveWebContents() {
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (content::WebContents* wc =
            browser->GetTabStripModel()->GetActiveWebContents()) {
      return wc;
    }
  }
  return nullptr;
}

bool IsCompanionUrl(const GURL& url) {
  return url.host() == "127.0.0.1" && url.port() == "9334";
}

// Prefer a real browsing tab over the Grok Search companion page.
content::WebContents* FindScreenshotTargetWebContents() {
  content::WebContents* fallback = nullptr;
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    TabStripModel* model = browser->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* wc = model->GetWebContentsAt(i);
      if (!wc)
        continue;
      const GURL url = wc->GetLastCommittedURL();
      if (IsCompanionUrl(url) || url.SchemeIs("chrome"))
        continue;
      if (model->GetActiveWebContents() == wc)
        return wc;
      fallback = wc;
    }
  }
  return fallback ? fallback : FindActiveWebContents();
}

void CaptureScreenshot(
    content::WebContents* wc,
    base::OnceCallback<void(base::DictValue)> callback) {
  if (!wc) {
    base::DictValue err;
    err.Set("error", "no tab to capture");
    std::move(callback).Run(std::move(err));
    return;
  }
  CaptureTabScreenshot(
      wc,
      base::BindOnce(
          [](base::OnceCallback<void(base::DictValue)> callback,
             content::WebContents* wc, base::DictValue result) {
            const std::string* data = result.FindString("data");
            if (!data || data->empty()) {
              const std::string* err_msg = result.FindString("error");
              base::DictValue err;
              err.Set("error", err_msg ? *err_msg : "screenshot failed");
              std::move(callback).Run(std::move(err));
              return;
            }
            base::DictValue ok;
            ok.Set("image", *data);
            ok.Set("mime_type", "image/png");
            ok.Set("url", wc->GetLastCommittedURL().spec());
            ok.Set("title", base::UTF16ToUTF8(wc->GetTitle()));
            std::move(callback).Run(std::move(ok));
          },
          std::move(callback), wc));
}

}  // namespace

struct GrokWebPendingEntry {
  std::string prompt;
  base::Time created;
};

base::NoDestructor<std::map<std::string, GrokWebPendingEntry>> g_grok_web_pending;

void PruneGrokWebPending() {
  const base::Time cutoff = base::Time::Now() - base::Minutes(10);
  for (auto it = g_grok_web_pending->begin();
       it != g_grok_web_pending->end();) {
    if (it->second.created < cutoff)
      it = g_grok_web_pending->erase(it);
    else
      ++it;
  }
}

std::string StoreGrokWebPending(const std::string& prompt) {
  PruneGrokWebPending();
  const std::string id = base::HexEncode(base::RandBytesAsVector(8));
  (*g_grok_web_pending)[id] = {prompt, base::Time::Now()};
  return id;
}

std::string TruncateForPrompt(const std::string& text, size_t max_len) {
  if (text.size() <= max_len)
    return text;
  return text.substr(0, max_len) + "\n\n[page content truncated]";
}

std::string BuildPageGrokWebPromptForAction(const std::string& action,
                                            const std::string& url,
                                            const std::string& title,
                                            const std::string& text) {
  std::string instruction;
  if (action == "analyze") {
    instruction =
        "Analyze this web page. Break down the key points, context, and "
        "implications. Do not invent content not present on the page.\n\n";
  } else if (action == "factcheck") {
    instruction =
        "Fact-check this web page. Assess the claims and note what is "
        "supported, unclear, or questionable. Do not invent content not "
        "present on the page.\n\n";
  } else if (action == "explain") {
    instruction =
        "Explain this web page clearly and simply, as if helping someone "
        "understand it. Do not invent content not present on the page.\n\n";
  } else {
    instruction =
        "Summarize this web page clearly and concisely. Use short paragraphs "
        "or bullet points. Focus on the main ideas, facts, and takeaways. Do "
        "not invent content not present on the page.\n\n";
  }
  const std::string page_title = title.empty() ? "Web page" : title;
  const std::string page_url = url.empty() ? "(unknown)" : url;
  return base::StringPrintf("%sTitle: %s\nURL: %s\n\nPage content:\n%s",
                              instruction.c_str(), page_title.c_str(),
                              page_url.c_str(),
                              TruncateForPrompt(text, 24000).c_str());
}

std::string BuildPageGrokWebPrompt(const std::string& text) {
  return BuildPageGrokWebPromptForAction("summarize", "", "", text);
}

std::string GetGrokWebPendingPrompt(const std::string& id) {
  PruneGrokWebPending();
  auto it = g_grok_web_pending->find(id);
  if (it == g_grok_web_pending->end())
    return {};
  return it->second.prompt;
}

void ConsumeGrokWebPendingPrompt(const std::string& id) {
  PruneGrokWebPending();
  g_grok_web_pending->erase(id);
}

std::string BuildPageSummarizePrompt(const std::string& url,
                                     const std::string& title,
                                     const std::string& text) {
  return base::StringPrintf(
      "Summarize this web page clearly and concisely for the user. "
      "Use short paragraphs or bullet points. Focus on the main ideas, "
      "facts, and takeaways. Do not invent content not present in the page.\n\n"
      "Title: %s\nURL: %s\n\nPage content:\n%s",
      title.c_str(), url.c_str(),
      TruncateForPrompt(text, 14000).c_str());
}

base::DictValue CreatePageChatConversation(const std::string& url,
                                           const std::string& title,
                                           const std::string& text,
                                           const std::string& summary,
                                           int gateway_port) {
  std::string message =
      base::StringPrintf("I'm reading this page:\n\n**%s**\n%s\n\n",
                         title.c_str(), url.c_str());
  if (!summary.empty()) {
    message += "Summary:\n" + summary + "\n\n";
  } else {
    const std::string excerpt = TruncateForPrompt(text, 4000);
    if (!excerpt.empty())
      message += "Excerpt:\n" + excerpt + "\n\n";
  }
  message +=
      "Help me understand this page. What are the key takeaways, and what "
      "questions should I be asking?";

  base::DictValue data = LoadSessions();
  base::ListValue* convs = data.FindList("conversations");
  if (!convs) {
    data.Set("conversations", base::ListValue());
    convs = data.FindList("conversations");
  }
  const std::string id = base::HexEncode(base::RandBytesAsVector(8));
  base::DictValue conv;
  conv.Set("id", id);
  conv.Set("title", title.empty() ? "Page chat" : title.substr(0, 48));
  conv.Set("session_id", base::Value());
  base::ListValue msgs;
  base::DictValue user_msg;
  user_msg.Set("role", "user");
  user_msg.Set("content", message);
  msgs.Append(user_msg.Clone());
  conv.Set("messages", std::move(msgs));
  convs->Append(conv.Clone());
  SaveSessions(data);

  base::DictValue result;
  result.Set("ok", true);
  result.Set("id", id);
  result.Set("chat_url",
              base::StringPrintf("http://127.0.0.1:%d/?conv=%s", gateway_port,
                                 id.c_str()));
  return result;
}

base::CommandLine BuildGrokChatCommand(const std::string& message,
                                       const std::string& session_id,
                                       const std::string& model,
                                       bool streaming,
                                       const std::string& rules) {
  base::CommandLine cmd(base::CommandLine::NO_PROGRAM);
  cmd.SetProgram(ResolveGrokBinary());
  cmd.AppendArg("-p");
  cmd.AppendArg(message);
  cmd.AppendArg("--output-format");
  cmd.AppendArg(streaming ? "streaming-json" : "json");
  cmd.AppendArg("--always-approve");
  cmd.AppendArg("-m");
  cmd.AppendArg(model);
  cmd.AppendArg("--max-turns");
  cmd.AppendArg("25");
  if (!rules.empty()) {
    cmd.AppendArg("--rules");
    cmd.AppendArg(rules);
  }
  if (!session_id.empty()) {
    cmd.AppendArg("-r");
    cmd.AppendArg(session_id);
  }
  return cmd;
}

base::CommandLine BuildGrokAgentCommand(const std::string& message,
                                        const std::string& session_id,
                                        const std::string& model,
                                        bool streaming,
                                        const base::FilePath& cwd,
                                        const std::string& rules) {
  base::CommandLine cmd =
      BuildGrokChatCommand(message, session_id, model, streaming, rules);
  if (!cwd.empty()) {
    cmd.AppendArg("--cwd");
    cmd.AppendArgPath(cwd);
  }
  return cmd;
}

constexpr char kAppBuildRules[] =
    "You are Grok Build, an app builder inside Xplorer. Work only inside the "
    "app directory (--cwd). Create and modify files to build working apps. "
    "Prefer simple HTML/CSS/JS static apps with index.html as the entry point. "
    "Do NOT start web servers or tell the user to run npm/python servers — "
    "Xplorer auto-hosts each app on its own localhost port. Use relative paths "
    "for assets (./style.css, ./app.js). Write a short README. Be concise in "
    "chat; put code in files.";

void RunGrokAgentStream(
    net::HttpServer* server,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    int connection_id,
    const std::string& conv_id,
    const std::string& app_id,
    const std::string& message,
    const std::string& session_id,
    const std::string& model,
    const base::FilePath& cwd) {
  base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE})
      ->PostTask(
          FROM_HERE,
          base::BindOnce(
              &PumpGrokStream, server, io_task_runner, connection_id,
              BuildGrokAgentCommand(message, session_id, model, true, cwd,
                                    kAppBuildRules),
              model, "app-build", GrokStreamKind::kAppBuild, conv_id, app_id));
}

base::CommandLine BuildPageSummarizeCommand(const std::string& url,
                                            const std::string& title,
                                            const std::string& text,
                                            const std::string& model,
                                            bool streaming) {
  return BuildGrokChatCommand(BuildPageSummarizePrompt(url, title, text), "",
                              model, streaming, kChatRules);
}

void RunGrokChatStream(
    net::HttpServer* server,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    int connection_id,
    std::string conv_id,
    std::string message,
    std::string session_id,
    std::string model) {
  const char* rules = ChatRulesForMessage(message);
  base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE})
      ->PostTask(FROM_HERE,
                 base::BindOnce(&PumpGrokStream, server, io_task_runner,
                                connection_id,
                                BuildGrokChatCommand(message, session_id, model,
                                                     true, rules),
                                model, "chat", GrokStreamKind::kChat, conv_id,
                                ""));
}

base::DictValue RunGrokSearch(const std::string& query,
                              const std::string& mode,
                              const std::string& model,
                              SearchImageInput image) {
  base::CommandLine cmd = BuildGrokSearchCommand(
      query, mode, model, false, image.data.empty() ? nullptr : &image);
  int exit_code = 0;
  std::string output;
  if (!base::GetAppOutputWithExitCode(cmd, &output, &exit_code) ||
      exit_code != 0) {
    base::DictValue err;
    err.Set("error", output.empty() ? "grok search failed" : output);
    return err;
  }
  std::string text;
  if (auto parsed = base::JSONReader::ReadDict(output, base::JSON_PARSE_RFC)) {
    if (const std::string* t = parsed->FindString("text"))
      text = *t;
  } else {
    text = output;
  }
  base::DictValue result = ParseSearchText(text, mode);
  result.Set("model", model);
  result.Set("model_label", ModelDisplayName(model));
  return result;
}

base::DictValue RunGrokChat(const std::string& message,
                            const std::string& session_id,
                            const std::string& model) {
  base::CommandLine cmd = BuildGrokChatCommand(
      message, session_id, model, false, ChatRulesForMessage(message));
  int exit_code = 0;
  std::string output;
  if (!base::GetAppOutputWithExitCode(cmd, &output, &exit_code) ||
      exit_code != 0) {
    base::DictValue err;
    std::string err_text = output.empty() ? "grok failed" : output;
    if (err_text.find("auth") != std::string::npos ||
        err_text.find("login") != std::string::npos) {
      err_text += " — run: grok login --oauth";
    }
    err.Set("error", err_text);
    return err;
  }
  if (auto parsed = base::JSONReader::ReadDict(output, base::JSON_PARSE_RFC))
    return std::move(*parsed);
  base::DictValue fallback;
  fallback.Set("text", output);
  if (!session_id.empty())
    fallback.Set("sessionId", session_id);
  return fallback;
}

bool ExtractSearchImage(const base::DictValue* body, SearchImageInput* out) {
  if (!body || !out)
    return false;
  const std::string* image = body->FindString("image");
  if (!image || image->empty())
    return false;
  if (image->size() > 2 * 1024 * 1024) {
    return false;
  }
  out->data = *image;
  if (const std::string* mime = body->FindString("image_mime");
      mime && !mime->empty()) {
    out->mime = *mime;
  }
  return true;
}

bool SearchRequestValid(const base::DictValue* body) {
  if (!body)
    return false;
  const std::string* query = body->FindString("query");
  if (query && !query->empty())
    return true;
  SearchImageInput image;
  return ExtractSearchImage(body, &image);
}

void ReplyJsonOnIO(net::HttpServer* server,
                   scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
                   int connection_id,
                   base::DictValue dict) {
  std::string json;
  base::JSONWriter::Write(dict, &json);
  io_task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](net::HttpServer* s, int cid, std::string body) {
            SendBytes(s, cid, net::HTTP_OK, std::move(body),
                      "application/json");
          },
          server, connection_id, std::move(json)));
}

void RunAsync(net::HttpServer* server,
              int connection_id,
              base::OnceCallback<base::DictValue()> work) {
  base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE})
      ->PostTaskAndReplyWithResult(
          FROM_HERE, std::move(work),
          base::BindOnce(
              [](net::HttpServer* s, int cid, base::DictValue result) {
                if (result.FindString("error")) {
                  SendJsonOnIO(s, cid, net::HTTP_INTERNAL_SERVER_ERROR,
                               std::move(result));
                } else {
                  SendJsonOnIO(s, cid, net::HTTP_OK, std::move(result));
                }
              },
              server, connection_id));
}

base::DictValue LoadCompanionSessions() {
  return LoadSessions();
}

void SaveCompanionSessions(const base::DictValue& data) {
  SaveSessions(data);
}

std::string ResolveConfiguredModel(const std::string* model_override) {
  return ResolveModel(model_override);
}

std::string ResolveAppBuildModel(const std::string* model_override) {
  if (model_override && !model_override->empty())
    return *model_override;
  return kSearchModel;
}

bool GrokNative::TryHandleRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info,
    net::HttpServer* server,
    int gateway_port,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner) {
  const std::string path = PathOnly(info.path);

  if (TryHandleAppRunRequest(connection_id, info, server, gateway_port))
    return true;

  if (TryHandleAppsRequest(connection_id, info, server, gateway_port,
                           io_task_runner)) {
    return true;
  }

  if (info.method == "OPTIONS") {
    net::HttpServerResponseInfo resp(net::HTTP_NO_CONTENT);
    resp.AddHeader("Access-Control-Allow-Origin", "*");
    resp.AddHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    resp.AddHeader("Access-Control-Allow-Headers", "Content-Type");
    server->SendResponse(connection_id, resp, TRAFFIC_ANNOTATION_FOR_TESTS);
    return true;
  }

  if (info.method == "GET" && path == "/favicon.ico") {
    if (ServeUiFile(server, connection_id, "favicon.ico"))
      return true;
    net::HttpServerResponseInfo resp(net::HTTP_NO_CONTENT);
    server->SendResponse(connection_id, resp, TRAFFIC_ANNOTATION_FOR_TESTS);
    return true;
  }

  // Static assets before /search page route (not app runtime files).
  if (info.method == "GET" && !base::StartsWith(path, "/run/") &&
      (base::EndsWith(path, ".css") || base::EndsWith(path, ".js") ||
       base::EndsWith(path, ".ico") || base::EndsWith(path, ".png") ||
       base::EndsWith(path, ".svg") || base::EndsWith(path, ".jpg") ||
       base::EndsWith(path, ".jpeg") || base::EndsWith(path, ".webp"))) {
    return ServeUiFile(server, connection_id, path.substr(path.rfind('/') + 1));
  }

  // Canonical shared toolbar markup — single source for the companion pages
  // and the native overlay bar (grok_web_bar.cc fetches this).
  if (info.method == "GET" &&
      (path == "/toolbar.html" || path == "/partials/toolbar")) {
    return ServeUiFile(server, connection_id, "toolbar.html");
  }

  if (info.method == "GET" && (path == "/" || path == "/index.html")) {
    if (WantsHtml(info))
      return ServeUiFile(server, connection_id, "index.html");
    return false;  // Agent discovery JSON handled by AgentGateway.
  }

  // Default-search handoff: the omnibox (Grok is the default engine) and the
  // AI "Grok" button hit GET /omnibox?q=<query>; store the query as a pending
  // grok-web prompt and 302 to grok.com, where the injector auto-submits it.
  if (info.method == "GET" &&
      (path == "/omnibox" ||
       base::StartsWith(path, "/omnibox?", base::CompareCase::SENSITIVE))) {
    std::string raw_q;
    const std::string& full = info.path;
    auto qpos = full.find('?');
    if (qpos != std::string::npos) {
      for (const auto& pair : base::SplitString(
               full.substr(qpos + 1), "&", base::TRIM_WHITESPACE,
               base::SPLIT_WANT_NONEMPTY)) {
        if (base::StartsWith(pair, "q=", base::CompareCase::SENSITIVE)) {
          raw_q = pair.substr(2);
          break;
        }
      }
    }
    const std::string query = base::UnescapeURLComponent(
        raw_q, base::UnescapeRule::REPLACE_PLUS_WITH_SPACE |
                   base::UnescapeRule::SPACES |
                   base::UnescapeRule::PATH_SEPARATORS |
                   base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS);
    std::string dest = "https://grok.com/";
    if (!query.empty())
      dest = "https://grok.com/#xplorer_grok=" + StoreGrokWebPending(query);
    net::HttpServerResponseInfo resp(net::HTTP_FOUND);
    resp.SetBody("", "text/plain");
    resp.AddHeader("Location", dest);
    resp.AddHeader("Cache-Control", "no-store");
    resp.AddHeader("Connection", "close");
    server->SendResponse(connection_id, resp, TRAFFIC_ANNOTATION_FOR_TESTS);
    return true;
  }

  if (info.method == "GET" && (path == "/search" || path == "/search/")) {
    return ServeUiFile(server, connection_id, "search.html");
  }

  if (info.method == "GET" && (path == "/welcome" || path == "/welcome/")) {
    return ServeUiFile(server, connection_id, "welcome.html");
  }

  if (info.method == "GET" && (path == "/settings" || path == "/settings/")) {
    return ServeUiFile(server, connection_id, "settings.html");
  }

  if (info.method == "GET" &&
      (path == "/apps" || path == "/apps/" || path == "/apps/new")) {
    return ServeUiFile(server, connection_id, "apps.html");
  }

  if (info.method == "GET" && (path == "/app" || path == "/app/")) {
    return ServeUiFile(server, connection_id, "app.html");
  }

  if (info.method == "GET" && (path == "/logs" || path == "/logs/")) {
    return ServeUiFile(server, connection_id, "logs.html");
  }

  if (info.method == "GET" &&
      (path == "/switch-home" || base::StartsWith(path, "/switch-home?"))) {
    const std::map<std::string, std::string> params = QueryParams(info.path);
    auto it = params.find("mode");
    std::string mode = kSearchHomeBuild;
    if (it != params.end()) {
      if (it->second == kSearchHomeWeb)
        mode = kSearchHomeWeb;
      else if (it->second == kSearchHomeWiki)
        mode = kSearchHomeWiki;
    }
    SetSearchHomeMode(mode);
    std::string dest;
    if (mode == kSearchHomeWeb)
      dest = base::StringPrintf("http://127.0.0.1:%d/search", gateway_port);
    else if (mode == kSearchHomeWiki)
      dest = kGrokWikiHomeURL;
    else
      dest = base::StringPrintf("http://127.0.0.1:%d/", gateway_port);
    auto qit = params.find("q");
    if (qit != params.end() && !qit->second.empty()) {
      dest += (dest.find('?') == std::string::npos ? "?" : "&");
      dest += "q=" + qit->second;
    }
    auto mit = params.find("m");
    if (mit != params.end() && !mit->second.empty()) {
      dest += (dest.find('?') == std::string::npos ? "?" : "&");
      dest += "mode=" + mit->second;
    }
    net::HttpServerResponseInfo resp(net::HTTP_FOUND);
    resp.SetBody("", "text/plain");
    resp.AddHeader("Location", dest);
    resp.AddHeader("Cache-Control", "no-store");
    resp.AddHeader("Connection", "close");
    server->SendResponse(connection_id, resp, TRAFFIC_ANNOTATION_FOR_TESTS);
    return true;
  }

  if (info.method == "GET" && path == "/api/status") {
    const std::string model = GetConfiguredModel();
    base::DictValue d;
    d.Set("ok", true);
    d.Set("native", true);
    d.Set("port", gateway_port);
    d.Set("grok", ResolveGrokBinary().value());
    d.Set("model", model);
    d.Set("model_label", ModelDisplayName(model));
    d.Set("models", ListGrokModels());
    SendJson(server, connection_id, net::HTTP_OK, std::move(d));
    return true;
  }

  if (info.method == "GET" && path == "/api/logs") {
    const std::map<std::string, std::string> params = QueryParams(info.path);
    std::string source_filter;
    std::string app_filter;
    size_t limit = 0;  // 0 = no cap (ring already bounded)
    if (auto it = params.find("source"); it != params.end())
      source_filter = it->second;
    if (auto it = params.find("app"); it != params.end())
      app_filter = it->second;
    if (auto it = params.find("limit"); it != params.end()) {
      int parsed = 0;
      if (base::StringToInt(it->second, &parsed) && parsed > 0)
        limit = static_cast<size_t>(parsed);
    }
    base::DictValue d;
    d.Set("ok", true);
    d.Set("events", SnapshotGatewayLogs(source_filter, app_filter, limit));
    SendJson(server, connection_id, net::HTTP_OK, std::move(d));
    return true;
  }

  // One-shot native tab organization (no MCP / grok agent loop).
  if (info.method == "POST" &&
      (path == "/api/browser/organize-tabs" ||
       path == "/browser/organize-tabs")) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](net::HttpServer* srv,
               scoped_refptr<base::SingleThreadTaskRunner> io, int cid) {
              BrowserApi::OrganizeTabs(base::BindOnce(
                  [](net::HttpServer* srv,
                     scoped_refptr<base::SingleThreadTaskRunner> io, int cid,
                     base::DictValue result) {
                    ReplyJsonOnIO(srv, io, cid, std::move(result));
                  },
                  srv, io, cid));
            },
            server, io_task_runner, connection_id));
    return true;
  }

  // Browser theme for companion UI (no auth — same as other native UI routes).
  if ((info.method == "GET" || info.method == "POST") &&
      (path == "/theme" || path == "/api/theme")) {
    if (info.method == "POST") {
      auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
      const std::string* scheme =
          body ? body->FindString("color_scheme") : nullptr;
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](net::HttpServer* srv,
                 scoped_refptr<base::SingleThreadTaskRunner> io, int cid,
                 std::string scheme) {
                BrowserApi::SetTheme(
                    scheme.empty() ? "system" : scheme,
                    base::BindOnce(
                        [](net::HttpServer* srv,
                           scoped_refptr<base::SingleThreadTaskRunner> io,
                           int cid, base::DictValue result) {
                          ReplyJsonOnIO(srv, io, cid, std::move(result));
                        },
                        srv, io, cid));
              },
              server, io_task_runner, connection_id,
              scheme ? *scheme : std::string()));
      return true;
    }
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](net::HttpServer* srv,
               scoped_refptr<base::SingleThreadTaskRunner> io, int cid) {
              BrowserApi::GetTheme(base::BindOnce(
                  [](net::HttpServer* srv,
                     scoped_refptr<base::SingleThreadTaskRunner> io, int cid,
                     base::DictValue result) {
                    ReplyJsonOnIO(srv, io, cid, std::move(result));
                  },
                  srv, io, cid));
            },
            server, io_task_runner, connection_id));
    return true;
  }

  if (info.method == "GET" && path == "/api/models") {
    base::DictValue d;
    d.Set("models", ListGrokModels());
    d.Set("model", GetConfiguredModel());
    SendJson(server, connection_id, net::HTTP_OK, std::move(d));
    return true;
  }

  if (info.method == "GET" && path == "/api/settings") {
    base::DictValue d = LoadSettings();
    if (!d.FindString("model"))
      d.Set("model", kDefaultModel);
    d.Set("model_label", ModelDisplayName(GetConfiguredModel()));
    d.Set("models", ListGrokModels());
    d.Set("search_home", GetSearchHomeMode());
    d.Set("grok_web_url", "https://grok.com/");
    d.Set("grok_build_url",
          "http://127.0.0.1:" + base::NumberToString(gateway_port) +
              "/search");
    d.Set("grok_wiki_url", kGrokWikiHomeURL);
    d.Set("welcome_completed", grok_companion::HasCompletedWelcome());
    d.Set("product_name", grok_companion::kProductName);
    EnrichSettingsResponse(&d, gateway_port);
    SendJson(server, connection_id, net::HTTP_OK, std::move(d));
    return true;
  }

  if (info.method == "POST" && path == "/api/settings") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    if (!body) {
      base::DictValue err;
      err.Set("error", "invalid json");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    const std::string* model = body->FindString("model");
    const std::string* search_model = body->FindString("search_model");
    const std::string* home = body->FindString("search_home");
    std::optional<bool> welcome = body->FindBool("welcome_completed");
    bool updated = false;
    if (model && !model->empty()) {
      SetConfiguredModel(*model);
      updated = true;
    }
    if (search_model && !search_model->empty()) {
      SetConfiguredSearchModel(*search_model);
      updated = true;
    }
    if (home && !home->empty()) {
      if (*home != kSearchHomeBuild && *home != kSearchHomeWeb &&
          *home != kSearchHomeWiki) {
        base::DictValue err;
        err.Set("error", "search_home must be 'build', 'web', or 'wiki'");
        SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
        return true;
      }
      SetSearchHomeMode(*home);
      updated = true;
    }
    if (welcome.has_value() && *welcome) {
      grok_companion::MarkWelcomeCompleted();
      updated = true;
    }
    if (!updated) {
      base::DictValue err;
      err.Set("error",
              "provide model, search_model, search_home, and/or "
              "welcome_completed");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    base::DictValue d;
    d.Set("ok", true);
    d.Set("model", GetConfiguredModel());
    d.Set("model_label", ModelDisplayName(GetConfiguredModel()));
    d.Set("search_home", GetSearchHomeMode());
    d.Set("grok_web_url", "https://grok.com/");
    d.Set("grok_build_url",
          "http://127.0.0.1:" + base::NumberToString(gateway_port) +
              "/search");
    d.Set("grok_wiki_url", kGrokWikiHomeURL);
    d.Set("welcome_completed", grok_companion::HasCompletedWelcome());
    d.Set("product_name", grok_companion::kProductName);
    EnrichSettingsResponse(&d, gateway_port);
    SendJson(server, connection_id, net::HTTP_OK, std::move(d));
    return true;
  }

  if (info.method == "GET" && path == "/api/conversations") {
    SendJson(server, connection_id, net::HTTP_OK, LoadSessions());
    return true;
  }

  if (info.method == "GET" &&
      (path == "/api/search" || path == "/api/search/stream")) {
    auto params = QueryParams(info.path);
    std::string query = params["q"];
    std::string mode = params.count("mode") ? params["mode"] : "web";
    const std::string* model_param = nullptr;
    std::string model_override;
    if (auto mit = params.find("model"); mit != params.end() && !mit->second.empty()) {
      model_override = mit->second;
      model_param = &model_override;
    }
    std::string model = ResolveSearchModel(mode, model_param);
    if (query.empty()) {
      base::DictValue err;
      err.Set("error",
              "use POST /api/search/stream or GET ?q=...&mode=web");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    SearchImageInput no_image;
    if (path == "/api/search/stream") {
      RunGrokSearchStream(server, io_task_runner, connection_id, query, mode,
                          model, std::move(no_image));
    } else {
      RunAsync(server, connection_id,
               base::BindOnce(&RunGrokSearch, query, mode, model,
                              std::move(no_image)));
    }
    return true;
  }

  if (info.method == "GET" &&
      base::StartsWith(path, "/api/conversations/") &&
      path.find("/message") == std::string::npos) {
    std::string id = path.substr(std::string("/api/conversations/").size());
    base::DictValue data = LoadSessions();
    const base::ListValue* convs = data.FindList("conversations");
    if (convs) {
      for (const auto& v : *convs) {
        if (!v.is_dict())
          continue;
        const std::string* cid = v.GetDict().FindString("id");
        if (cid && *cid == id) {
          SendJson(server, connection_id, net::HTTP_OK, v.GetDict().Clone());
          return true;
        }
      }
    }
    base::DictValue err;
    err.Set("error", "not found");
    SendJson(server, connection_id, net::HTTP_NOT_FOUND, std::move(err));
    return true;
  }

  if (info.method == "POST" && path == "/api/page/summarize/stream") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* url = body ? body->FindString("url") : nullptr;
    const std::string* title = body ? body->FindString("title") : nullptr;
    const std::string* text = body ? body->FindString("text") : nullptr;
    if (!text || text->empty()) {
      base::DictValue err;
      err.Set("error", "page text required");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    const std::string* model_body = body ? body->FindString("model") : nullptr;
    const std::string model = ResolveModel(model_body);
    const std::string page_url = url ? *url : "";
    const std::string page_title = title ? *title : "Web page";
    base::ThreadPool::CreateSequencedTaskRunner(
        {base::MayBlock(), base::TaskPriority::USER_VISIBLE})
        ->PostTask(
            FROM_HERE,
            base::BindOnce(
                &PumpGrokStream, server, io_task_runner, connection_id,
                BuildPageSummarizeCommand(page_url, page_title, *text, model,
                                          true),
                model, "summarize", GrokStreamKind::kChat, "", ""));
    return true;
  }

  if (info.method == "POST" && path == "/api/page/grok-web") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* url = body ? body->FindString("url") : nullptr;
    const std::string* title = body ? body->FindString("title") : nullptr;
    const std::string* text = body ? body->FindString("text") : nullptr;
    const std::string* action = body ? body->FindString("action") : nullptr;
    const std::string action_id = action ? *action : "summarize";
    if (action_id == "open") {
      base::DictValue result;
      result.Set("ok", true);
      result.Set("grok_url", "https://grok.com/");
      SendJson(server, connection_id, net::HTTP_OK, std::move(result));
      return true;
    }
    const std::string* query = body ? body->FindString("query") : nullptr;
    if (query && !query->empty()) {
      const std::string id = StoreGrokWebPending(*query);
      LOG(INFO) << "[grok-web] stored search query id=" << id
                << " chars=" << query->size();
      base::DictValue result;
      result.Set("ok", true);
      result.Set("id", id);
      result.Set("grok_url", "https://grok.com/#xplorer_grok=" + id);
      SendJson(server, connection_id, net::HTTP_OK, std::move(result));
      return true;
    }
    if (!text || text->empty()) {
      base::DictValue err;
      err.Set("error", "query or page text required");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    const std::string page_url = url ? *url : "";
    const std::string page_title = title ? *title : "Web page";
    const std::string prompt = BuildPageGrokWebPromptForAction(
        action_id, page_url, page_title, *text);
    const std::string id = StoreGrokWebPending(prompt);
    LOG(INFO) << "[grok-web] stored pending id=" << id
              << " action=" << action_id << " text_chars=" << text->size()
              << " prompt_chars=" << prompt.size();
    base::DictValue result;
    result.Set("ok", true);
    result.Set("id", id);
    result.Set("grok_url", "https://grok.com/#xplorer_grok=" + id);
    SendJson(server, connection_id, net::HTTP_OK, std::move(result));
    return true;
  }

  if (info.method == "GET" && path == "/api/page/grok-web/pending") {
    std::string id;
    const auto params = QueryParams(info.path);
    auto it = params.find("id");
    if (it != params.end())
      id = it->second;
    if (id.empty()) {
      base::DictValue err;
      err.Set("error", "id required");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    const std::string prompt = GetGrokWebPendingPrompt(id);
    if (prompt.empty()) {
      base::DictValue err;
      err.Set("error", "not found");
      SendJson(server, connection_id, net::HTTP_NOT_FOUND, std::move(err));
      return true;
    }
    base::DictValue result;
    result.Set("ok", true);
    result.Set("prompt", prompt);
    SendJson(server, connection_id, net::HTTP_OK, std::move(result));
    return true;
  }

  if (info.method == "POST" && path == "/api/page/grok-web/consumed") {
    std::string id;
    const auto params = QueryParams(info.path);
    auto it = params.find("id");
    if (it != params.end())
      id = it->second;
    if (id.empty()) {
      base::DictValue err;
      err.Set("error", "id required");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    ConsumeGrokWebPendingPrompt(id);
    base::DictValue result;
    result.Set("ok", true);
    SendJson(server, connection_id, net::HTTP_OK, std::move(result));
    return true;
  }

  if (info.method == "POST" && path == "/api/page/start-chat") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* url = body ? body->FindString("url") : nullptr;
    const std::string* title = body ? body->FindString("title") : nullptr;
    const std::string* text = body ? body->FindString("text") : nullptr;
    const std::string* summary = body ? body->FindString("summary") : nullptr;
    if (!url || !title) {
      base::DictValue err;
      err.Set("error", "url and title required");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    const std::string page_text = text ? *text : "";
    const std::string page_summary = summary ? *summary : "";
    SendJson(server, connection_id, net::HTTP_OK,
             CreatePageChatConversation(*url, *title, page_text, page_summary,
                                        gateway_port));
    return true;
  }

  if (info.method == "POST" && path == "/api/screenshot") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* tab_id = body ? body->FindString("tab_id") : nullptr;
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](net::HttpServer* srv,
               scoped_refptr<base::SingleThreadTaskRunner> io, int cid,
               std::string tab) {
              content::WebContents* wc = nullptr;
              if (!tab.empty())
                wc = FindWebContentsByTabId(tab);
              if (!wc)
                wc = FindScreenshotTargetWebContents();
              CaptureScreenshot(
                  wc,
                  base::BindOnce(
                      [](net::HttpServer* srv,
                         scoped_refptr<base::SingleThreadTaskRunner> io, int cid,
                         base::DictValue result) {
                        if (result.FindString("error")) {
                          ReplyJsonOnIO(srv, io, cid, std::move(result));
                        } else {
                          ReplyJsonOnIO(srv, io, cid, std::move(result));
                        }
                      },
                      srv, io, cid));
            },
            server, io_task_runner, connection_id,
            tab_id ? *tab_id : std::string()));
    return true;
  }

  if (info.method == "POST" &&
      (path == "/api/search" || path == "/api/search/stream")) {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* query = body ? body->FindString("query") : nullptr;
    const std::string* mode = body ? body->FindString("mode") : nullptr;
    if (!SearchRequestValid(body ? &*body : nullptr)) {
      base::DictValue err;
      if (body && body->FindString("image") &&
          body->FindString("image")->size() > 2 * 1024 * 1024) {
        err.Set("error", "image too large — compress or resize before upload");
      } else {
        err.Set("error", "empty query (or attach an image for vision search)");
      }
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    const std::string search_query = query ? *query : std::string();
    const std::string search_mode = mode ? *mode : "web";
    const std::string* model_body = body ? body->FindString("model") : nullptr;
    std::string model = ResolveSearchModel(search_mode, model_body);
    SearchImageInput image;
    ExtractSearchImage(body ? &*body : nullptr, &image);
    const bool stream =
        path == "/api/search/stream" ||
        (body && body->FindBool("stream").value_or(false));
    if (stream) {
      RunGrokSearchStream(server, io_task_runner, connection_id, search_query,
                          search_mode, model, std::move(image));
    } else {
      RunAsync(server, connection_id,
               base::BindOnce(&RunGrokSearch, search_query, search_mode, model,
                              std::move(image)));
    }
    return true;
  }

  if (info.method == "POST" && path == "/api/conversations") {
    base::DictValue data = LoadSessions();
    base::ListValue* convs = data.FindList("conversations");
    if (!convs) {
      data.Set("conversations", base::ListValue());
      convs = data.FindList("conversations");
    }
    base::DictValue conv;
    conv.Set("id", base::HexEncode(base::RandBytesAsVector(8)));
    conv.Set("title", "New chat");
    conv.Set("session_id", base::Value());
    conv.Set("messages", base::ListValue());
    convs->Append(conv.Clone());
    SaveSessions(data);
    SendJson(server, connection_id, net::HTTP_OK, std::move(conv));
    return true;
  }

  if (info.method == "POST" && base::StartsWith(path, "/api/conversations/") &&
      (base::EndsWith(path, "/message") ||
       base::EndsWith(path, "/message/stream"))) {
    const bool chat_stream = base::EndsWith(path, "/message/stream");
    const std::string prefix = "/api/conversations/";
    std::string rest = path.substr(prefix.size());
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) {
      base::DictValue err;
      err.Set("error", "invalid conversation path");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    std::string conv_id = rest.substr(0, slash);
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* message = body ? body->FindString("message") : nullptr;
    const std::string* model_body = body ? body->FindString("model") : nullptr;
    if (!message || message->empty()) {
      base::DictValue err;
      err.Set("error", "empty message");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    std::string model = ResolveChatModel(*message, model_body);
    base::DictValue data = LoadSessions();
    base::ListValue* convs = data.FindList("conversations");
    base::DictValue* conv = nullptr;
    if (convs) {
      for (auto& v : *convs) {
        if (!v.is_dict())
          continue;
        const std::string* cid = v.GetDict().FindString("id");
        if (cid && *cid == conv_id) {
          conv = &v.GetDict();
          break;
        }
      }
    }
    if (!conv) {
      base::DictValue err;
      err.Set("error", "conversation not found");
      SendJson(server, connection_id, net::HTTP_NOT_FOUND, std::move(err));
      return true;
    }
    base::ListValue* msgs = conv->FindList("messages");
    if (!msgs) {
      conv->Set("messages", base::ListValue());
      msgs = conv->FindList("messages");
    }
    base::DictValue user_msg;
    user_msg.Set("role", "user");
    user_msg.Set("content", *message);
    msgs->Append(user_msg.Clone());
    if (const std::string* title = conv->FindString("title");
        title && *title == "New chat") {
      conv->Set("title", message->substr(0, std::min<size_t>(48, message->size())));
    }
    std::string session_id;
    if (const std::string* sid = conv->FindString("session_id"))
      session_id = *sid;
    SaveSessions(data);
    if (chat_stream) {
      if (MessageWantsOrganizeTabs(*message)) {
        RunOrganizeTabsFastPath(server, io_task_runner, connection_id,
                                conv_id);
        return true;
      }
      RunGrokChatStream(server, io_task_runner, connection_id, conv_id,
                        *message, session_id, model);
      return true;
    }
    RunAsync(
        server, connection_id,
        base::BindOnce(
            [](std::string cid, std::string sid, std::string message,
               std::string model) {
              base::DictValue result = RunGrokChat(message, sid, model);
              base::DictValue data = LoadSessions();
              base::ListValue* convs = data.FindList("conversations");
              if (convs) {
                for (auto& v : *convs) {
                  if (!v.is_dict())
                    continue;
                  const std::string* id = v.GetDict().FindString("id");
                  if (!id || *id != cid)
                    continue;
                  if (const std::string* new_sid =
                          result.FindString("sessionId")) {
                    v.GetDict().Set("session_id", *new_sid);
                  }
                  const std::string* text = result.FindString("text");
                  if (text) {
                    base::DictValue assistant;
                    assistant.Set("role", "assistant");
                    assistant.Set("content", *text);
                    if (base::ListValue* msgs =
                            v.GetDict().FindList("messages")) {
                      msgs->Append(std::move(assistant));
                    }
                  }
                  break;
                }
              }
              SaveSessions(data);
              if (const std::string* text = result.FindString("text"))
                result.Set("reply", *text);
              return result;
            },
            conv_id, session_id, *message, model));
    return true;
  }

  if (info.method == "POST" && base::StartsWith(path, "/api/conversations/") &&
      base::EndsWith(path, "/rename")) {
    const std::string prefix = "/api/conversations/";
    std::string conv_id =
        path.substr(prefix.size(), path.size() - prefix.size() - 7);
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* title = body ? body->FindString("title") : nullptr;
    if (!title || title->empty()) {
      base::DictValue err;
      err.Set("error", "title required");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    base::DictValue data = LoadSessions();
    base::ListValue* convs = data.FindList("conversations");
    base::DictValue* conv = nullptr;
    if (convs) {
      for (auto& v : *convs) {
        if (!v.is_dict())
          continue;
        const std::string* cid = v.GetDict().FindString("id");
        if (cid && *cid == conv_id) {
          conv = &v.GetDict();
          break;
        }
      }
    }
    if (!conv) {
      base::DictValue err;
      err.Set("error", "conversation not found");
      SendJson(server, connection_id, net::HTTP_NOT_FOUND, std::move(err));
      return true;
    }
    conv->Set("title", title->substr(0, std::min<size_t>(80, title->size())));
    SaveSessions(data);
    SendJson(server, connection_id, net::HTTP_OK, conv->Clone());
    return true;
  }

  if (info.method == "DELETE" &&
      base::StartsWith(path, "/api/conversations/")) {
    std::string id = path.substr(std::string("/api/conversations/").size());
    base::DictValue data = LoadSessions();
    base::ListValue filtered;
    if (const base::ListValue* convs = data.FindList("conversations")) {
      for (const auto& v : *convs) {
        if (!v.is_dict())
          continue;
        const std::string* cid = v.GetDict().FindString("id");
        if (cid && *cid != id)
          filtered.Append(v.Clone());
      }
    }
    data.Set("conversations", std::move(filtered));
    SaveSessions(data);
    base::DictValue ok;
    ok.Set("ok", true);
    SendJson(server, connection_id, net::HTTP_OK, std::move(ok));
    return true;
  }

  return false;
}

}  // namespace agent_gateway