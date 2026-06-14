// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/app_store.h"
#include "chrome/browser/agent_gateway/xplorer_paths.h"

#include <map>
#include <set>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/rand_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "chrome/browser/agent_gateway/grok_native.h"
#include "net/http/http_status_code.h"
#include "net/server/http_server_response_info.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "url/gurl.h"

namespace agent_gateway {
namespace {

constexpr char kStatusIdle[] = "idle";
constexpr char kStatusBuilding[] = "building";
constexpr char kStatusReady[] = "ready";
constexpr char kStatusError[] = "error";
constexpr int kAppRuntimePortBase = 9340;
constexpr int kAppRuntimePortSpan = 600;

struct AppRuntimeServer {
  base::Process process;
  int port = 0;
};

base::NoDestructor<std::set<std::string>> g_active_app_builds;
base::NoDestructor<std::map<std::string, AppRuntimeServer>> g_app_runtime_servers;

std::string PathOnly(const std::string& path) {
  return base::SplitString(path, "?", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY)[0];
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

base::FilePath AppsRootDir() {
  base::FilePath dir = xplorer_paths::DataDir();
  if (dir.empty())
    return base::FilePath();
  return dir.AppendASCII("apps");
}

base::FilePath RegistryFile() {
  return AppsRootDir().AppendASCII("registry.json");
}

std::string NowIso() {
  return base::NumberToString(
      base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds());
}

std::string NewId() {
  return base::HexEncode(base::RandBytesAsVector(8));
}

bool EnsureAppsRoot() {
  base::FilePath root = AppsRootDir();
  return !root.empty() && base::CreateDirectory(root);
}

base::DictValue LoadRegistry() {
  if (!EnsureAppsRoot())
    return base::DictValue();
  std::string json;
  if (!base::ReadFileToString(RegistryFile(), &json))
    return base::DictValue();
  auto parsed = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  return parsed ? std::move(*parsed) : base::DictValue();
}

void SaveRegistry(const base::DictValue& data) {
  if (!EnsureAppsRoot())
    return;
  std::string json;
  base::JSONWriter::Write(data, &json);
  base::WriteFile(RegistryFile(), json);
}

base::ListValue* AppsList(base::DictValue& registry) {
  base::ListValue* apps = registry.FindList("apps");
  if (!apps) {
    registry.Set("apps", base::ListValue());
    apps = registry.FindList("apps");
  }
  return apps;
}

base::DictValue* FindAppDict(base::DictValue& registry, const std::string& id) {
  base::ListValue* apps = AppsList(registry);
  if (!apps)
    return nullptr;
  for (auto& v : *apps) {
    if (!v.is_dict())
      continue;
    const std::string* aid = v.GetDict().FindString("id");
    if (aid && *aid == id)
      return &v.GetDict();
  }
  return nullptr;
}

base::FilePath ResolveAppPath(const base::DictValue& app);
bool IsPathInside(const base::FilePath& root, const base::FilePath& child);
std::string GuessContentType(const base::FilePath& path);

std::string AppStatus(const base::DictValue& app) {
  if (const std::string* id = app.FindString("id");
      id && g_active_app_builds->count(*id))
    return kStatusBuilding;
  const std::string* status = app.FindString("status");
  return status ? *status : kStatusIdle;
}

void ReconcileRegistryStates(base::DictValue& registry) {
  bool changed = false;
  base::ListValue* apps = AppsList(registry);
  if (!apps)
    return;
  for (auto& v : *apps) {
    if (!v.is_dict())
      continue;
    base::DictValue& app = v.GetDict();
    const std::string* id = app.FindString("id");
    if (!id || id->empty())
      continue;
    const std::string* status = app.FindString("status");
    if (status && *status == kStatusBuilding &&
        !g_active_app_builds->count(*id)) {
      app.Set("status", kStatusIdle);
      changed = true;
    }
    if (status && *status == kStatusError && app.FindString("last_error")) {
      /* keep error state visible */
    }
  }
  if (changed)
    SaveRegistry(registry);
}

std::set<int> UsedRuntimePorts(const base::DictValue& registry) {
  std::set<int> used;
  if (const base::ListValue* apps = registry.FindList("apps")) {
    for (const auto& v : *apps) {
      if (!v.is_dict())
        continue;
      if (std::optional<int> p = v.GetDict().FindInt("runtime_port"))
        used.insert(*p);
    }
  }
  return used;
}

int DefaultRuntimePort(const std::string& app_id) {
  uint32_t hash = 0;
  for (unsigned char c : app_id)
    hash = hash * 31u + c;
  return kAppRuntimePortBase + static_cast<int>(hash % kAppRuntimePortSpan);
}

int AllocateRuntimePort(const std::string& app_id, const base::DictValue& registry) {
  std::set<int> used = UsedRuntimePorts(registry);
  int port = DefaultRuntimePort(app_id);
  for (int i = 0; i < kAppRuntimePortSpan; ++i) {
    int candidate = kAppRuntimePortBase + ((port - kAppRuntimePortBase + i) %
                                           kAppRuntimePortSpan);
    if (!used.count(candidate))
      return candidate;
  }
  return port;
}

int GetOrAssignRuntimePort(base::DictValue& registry, base::DictValue* app) {
  if (!app)
    return 0;
  const std::string* app_id = app->FindString("id");
  if (!app_id || app_id->empty())
    return 0;
  if (std::optional<int> existing = app->FindInt("runtime_port"))
    return *existing;
  const int port = AllocateRuntimePort(*app_id, registry);
  app->Set("runtime_port", port);
  app->Set("updated_at", NowIso());
  SaveRegistry(registry);
  return port;
}

void StopAppRuntimeServer(const std::string& app_id) {
  auto it = g_app_runtime_servers->find(app_id);
  if (it == g_app_runtime_servers->end())
    return;
  if (it->second.process.IsValid()) {
    it->second.process.Terminate(0, true);
  }
  g_app_runtime_servers->erase(it);
}

bool LaunchAppRuntimeServer(const std::string& app_id,
                            const base::FilePath& app_path,
                            int port) {
  if (app_path.empty() || port <= 0)
    return false;
  auto it = g_app_runtime_servers->find(app_id);
  if (it != g_app_runtime_servers->end() && it->second.process.IsValid() &&
      it->second.port == port) {
    int exit_code = 0;
    if (!it->second.process.WaitForExitWithTimeout(base::TimeDelta(),
                                                   &exit_code)) {
      return true;
    }
  }
  StopAppRuntimeServer(app_id);

  base::FilePath python = base::FilePath("/usr/bin/python3");
  if (!base::PathExists(python))
    python = base::FilePath("/usr/local/bin/python3");
  if (!base::PathExists(python))
    return false;

  base::CommandLine cmd(python);
  cmd.AppendArg("-m");
  cmd.AppendArg("http.server");
  cmd.AppendArg(base::NumberToString(port));
  cmd.AppendArg("--bind");
  cmd.AppendArg("127.0.0.1");
  cmd.AppendArg("--directory");
  cmd.AppendArgPath(app_path);

  base::LaunchOptions options;
  options.current_directory = app_path;
  base::Process process = base::LaunchProcess(cmd, options);
  if (!process.IsValid())
    return false;
  (*g_app_runtime_servers)[app_id] = {std::move(process), port};
  LOG(INFO) << "[apps] runtime server app=" << app_id << " port=" << port
            << " path=" << app_path.value();
  return true;
}

void EnsureAppRuntime(base::DictValue& registry, base::DictValue* app) {
  if (!app)
    return;
  const std::string* app_id = app->FindString("id");
  if (!app_id || app_id->empty())
    return;
  const int port = GetOrAssignRuntimePort(registry, app);
  base::FilePath app_path = ResolveAppPath(*app);
  if (!app_path.empty())
    LaunchAppRuntimeServer(*app_id, app_path, port);
}

void SetAppRuntimeUrls(base::DictValue& out,
                       const base::DictValue& app,
                       int gateway_port) {
  const std::string* app_id = app.FindString("id");
  if (!app_id || app_id->empty())
    return;
  const int runtime_port = app.FindInt("runtime_port").value_or(0);
  out.Set("runtime_port", runtime_port);
  out.Set("runtime_url",
          base::StringPrintf("http://127.0.0.1:%d/run/%s/", gateway_port,
                             app_id->c_str()));
  if (runtime_port > 0) {
    out.Set("open_url",
            base::StringPrintf("http://127.0.0.1:%d/", runtime_port));
  } else {
    out.Set("open_url",
            base::StringPrintf("http://127.0.0.1:%d/run/%s/", gateway_port,
                               app_id->c_str()));
  }
}

base::DictValue AppToJson(const base::DictValue& app, int gateway_port) {
  base::DictValue out = app.Clone();
  out.Set("status", AppStatus(app));
  SetAppRuntimeUrls(out, app, gateway_port);
  if (const std::string* sid = app.FindString("session_id")) {
    const std::string* path = app.FindString("path");
    if (path && !path->empty()) {
      if (!sid->empty())
        out.Set("cli_command",
                base::StringPrintf("grok -r %s --cwd %s", sid->c_str(),
                                   path->c_str()));
      else
        out.Set("cli_command",
                base::StringPrintf("grok --cwd %s", path->c_str()));
    }
  }
  return out;
}

bool ServeAppStaticFile(net::HttpServer* server,
                        int connection_id,
                        const base::FilePath& app_path,
                        const std::string& rel_path) {
  std::string rel = rel_path.empty() ? "index.html" : rel_path;
  base::FilePath target = app_path.AppendASCII(rel);
  if (!IsPathInside(app_path, target) || !base::PathExists(target)) {
    base::DictValue err;
    err.Set("error", "file not found");
    SendJson(server, connection_id, net::HTTP_NOT_FOUND, std::move(err));
    return true;
  }
  std::string bytes;
  base::ReadFileToString(target, &bytes);
  SendBytes(server, connection_id, net::HTTP_OK, std::move(bytes),
            GuessContentType(target).c_str());
  return true;
}

std::string CreateAppConversation(const std::string& app_id,
                                  const std::string& title) {
  base::DictValue data = LoadCompanionSessions();
  base::ListValue* convs = data.FindList("conversations");
  if (!convs) {
    data.Set("conversations", base::ListValue());
    convs = data.FindList("conversations");
  }
  const std::string conv_id = NewId();
  base::DictValue conv;
  conv.Set("id", conv_id);
  conv.Set("title", title);
  conv.Set("session_id", base::Value());
  conv.Set("kind", "app");
  conv.Set("app_id", app_id);
  conv.Set("messages", base::ListValue());
  convs->Append(conv.Clone());
  SaveCompanionSessions(data);
  return conv_id;
}

void AppendUserMessage(const std::string& conv_id, const std::string& message) {
  base::DictValue data = LoadCompanionSessions();
  base::ListValue* convs = data.FindList("conversations");
  if (!convs)
    return;
  for (auto& v : *convs) {
    if (!v.is_dict())
      continue;
    const std::string* cid = v.GetDict().FindString("id");
    if (!cid || *cid != conv_id)
      continue;
    base::ListValue* msgs = v.GetDict().FindList("messages");
    if (!msgs) {
      v.GetDict().Set("messages", base::ListValue());
      msgs = v.GetDict().FindList("messages");
    }
    base::DictValue user_msg;
    user_msg.Set("role", "user");
    user_msg.Set("content", message);
    msgs->Append(std::move(user_msg));
    break;
  }
  SaveCompanionSessions(data);
}

void SetAppField(base::DictValue& app,
                 const std::string& key,
                 const std::string& value) {
  app.Set(key, value);
  app.Set("updated_at", NowIso());
}

void MarkAppBuilding(const std::string& app_id) {
  g_active_app_builds->insert(app_id);
  base::DictValue registry = LoadRegistry();
  if (base::DictValue* app = FindAppDict(registry, app_id)) {
    SetAppField(*app, "status", kStatusBuilding);
    SaveRegistry(registry);
  }
}

std::string GuessContentType(const base::FilePath& path) {
  const std::string ext = base::ToLowerASCII(path.Extension());
  if (ext == ".html" || ext == ".htm")
    return "text/html";
  if (ext == ".css")
    return "text/css";
  if (ext == ".js")
    return "application/javascript";
  if (ext == ".json")
    return "application/json";
  if (ext == ".png")
    return "image/png";
  if (ext == ".jpg" || ext == ".jpeg")
    return "image/jpeg";
  if (ext == ".svg")
    return "image/svg+xml";
  return "application/octet-stream";
}

bool IsPathInside(const base::FilePath& root, const base::FilePath& child) {
  if (root.empty() || child.empty())
    return false;
  base::FilePath abs_root = base::MakeAbsoluteFilePath(root);
  base::FilePath abs_child = base::MakeAbsoluteFilePath(child);
  if (abs_root.empty() || abs_child.empty())
    return false;
  return abs_child == abs_root || abs_root.IsParent(abs_child);
}

base::FilePath ResolveAppPath(const base::DictValue& app) {
  const std::string* path = app.FindString("path");
  if (!path || path->empty())
    return base::FilePath();
  return base::FilePath(*path);
}

std::string DefaultAppName(const std::string& prompt) {
  if (prompt.empty())
    return "New App";
  std::string name = prompt.substr(0, std::min<size_t>(40, prompt.size()));
  base::TrimWhitespaceASCII(name, base::TRIM_ALL, &name);
  return name.empty() ? "New App" : name;
}

}  // namespace

bool TryHandleAppRunRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info,
    net::HttpServer* server,
    int gateway_port) {
  if (info.method != "GET")
    return false;
  const std::string path = PathOnly(info.path);
  const std::string prefix = "/run/";
  if (!base::StartsWith(path, prefix))
    return false;

  std::string rest = path.substr(prefix.size());
  if (rest.empty()) {
    net::HttpServerResponseInfo resp(net::HTTP_BAD_REQUEST);
    resp.SetBody("{\"error\":\"app id required\"}", "application/json");
    server->SendResponse(connection_id, resp, TRAFFIC_ANNOTATION_FOR_TESTS);
    return true;
  }

  const size_t slash = rest.find('/');
  const std::string app_id =
      slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string rel =
      slash == std::string::npos ? "" : rest.substr(slash + 1);

  if (app_id.empty()) {
    base::DictValue err;
    err.Set("error", "app id required");
    SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
    return true;
  }

  if (slash == std::string::npos) {
    net::HttpServerResponseInfo resp(net::HTTP_FOUND);
    resp.AddHeader(
        "Location",
        base::StringPrintf("http://127.0.0.1:%d/run/%s/", gateway_port,
                           app_id.c_str()));
    resp.AddHeader("Cache-Control", "no-store");
    server->SendResponse(connection_id, resp, TRAFFIC_ANNOTATION_FOR_TESTS);
    return true;
  }

  base::DictValue registry = LoadRegistry();
  base::DictValue* app = FindAppDict(registry, app_id);
  if (!app) {
    base::DictValue err;
    err.Set("error", "app not found");
    SendJson(server, connection_id, net::HTTP_NOT_FOUND, std::move(err));
    return true;
  }

  EnsureAppRuntime(registry, app);
  base::FilePath app_path = ResolveAppPath(*app);
  return ServeAppStaticFile(server, connection_id, app_path, rel);
}

void OnAppBuildStreamFinished(const std::string& app_id,
                              const std::string& conv_id,
                              int exit_code,
                              const std::string& session_id,
                              const std::string& full_text) {
  g_active_app_builds->erase(app_id);
  base::DictValue registry = LoadRegistry();
  base::DictValue* app = FindAppDict(registry, app_id);
  if (!app)
    return;
  if (exit_code == 0) {
    SetAppField(*app, "status", kStatusReady);
    app->Set("last_error", "");
  } else {
    SetAppField(*app, "status", kStatusError);
    app->Set("last_error",
             "grok build failed (exit " + base::NumberToString(exit_code) + ")");
  }
  if (!session_id.empty())
    app->Set("session_id", session_id);
  SaveRegistry(registry);

  if (!session_id.empty()) {
    base::DictValue data = LoadCompanionSessions();
    base::ListValue* convs = data.FindList("conversations");
    if (convs) {
      for (auto& v : *convs) {
        if (!v.is_dict())
          continue;
        const std::string* cid = v.GetDict().FindString("id");
        if (cid && *cid == conv_id)
          v.GetDict().Set("session_id", session_id);
      }
    }
    SaveCompanionSessions(data);
  }
  LOG(INFO) << "[apps] build finished app=" << app_id
            << " exit=" << exit_code
            << " session=" << session_id;
}

bool TryHandleAppsRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info,
    net::HttpServer* server,
    int gateway_port,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner) {
  const std::string path = PathOnly(info.path);
  const std::string prefix = "/api/apps/";

  if (info.method == "GET" && path == "/api/apps") {
    base::DictValue registry = LoadRegistry();
    ReconcileRegistryStates(registry);
    base::ListValue* apps = registry.FindList("apps");
    base::ListValue out_apps;
    if (apps) {
      for (const auto& v : *apps) {
        if (!v.is_dict())
          continue;
        out_apps.Append(AppToJson(v.GetDict(), gateway_port));
      }
    }
    base::DictValue result;
    result.Set("ok", true);
    result.Set("apps_root", AppsRootDir().value());
    result.Set("apps", std::move(out_apps));
    SendJson(server, connection_id, net::HTTP_OK, std::move(result));
    return true;
  }

  if (info.method == "POST" && path == "/api/apps/import") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* import_path = body ? body->FindString("path") : nullptr;
    const std::string* name_body = body ? body->FindString("name") : nullptr;
    if (!import_path || import_path->empty()) {
      base::DictValue err;
      err.Set("error", "path required");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    base::FilePath abs = base::MakeAbsoluteFilePath(base::FilePath(*import_path));
    if (abs.empty() || !base::DirectoryExists(abs)) {
      base::DictValue err;
      err.Set("error", "path not found");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    const std::string id = NewId();
    const std::string name =
        name_body && !name_body->empty() ? *name_body : abs.BaseName().value();
    const std::string conv_id = CreateAppConversation(id, name);
    base::DictValue app;
    app.Set("id", id);
    app.Set("name", name);
    app.Set("path", abs.value());
    app.Set("conversation_id", conv_id);
    app.Set("session_id", "");
    app.Set("status", kStatusReady);
    app.Set("imported", true);
    app.Set("created_at", NowIso());
    app.Set("updated_at", NowIso());
    app.Set("last_error", "");
    base::DictValue registry = LoadRegistry();
    AppsList(registry)->Append(app.Clone());
    if (base::DictValue* stored = FindAppDict(registry, id))
      GetOrAssignRuntimePort(registry, stored);
    SaveRegistry(registry);
    base::DictValue result;
    result.Set("ok", true);
    if (base::DictValue* stored = FindAppDict(registry, id))
      result.Set("app", AppToJson(*stored, gateway_port));
    else
      result.Set("app", AppToJson(app, gateway_port));
    SendJson(server, connection_id, net::HTTP_OK, std::move(result));
    return true;
  }

  if (info.method == "POST" && path == "/api/apps") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* prompt = body ? body->FindString("prompt") : nullptr;
    const std::string* name_body = body ? body->FindString("name") : nullptr;
    if (!prompt || prompt->empty()) {
      base::DictValue err;
      err.Set("error", "prompt required");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    if (!EnsureAppsRoot()) {
      base::DictValue err;
      err.Set("error", "could not create apps directory");
      SendJson(server, connection_id, net::HTTP_INTERNAL_SERVER_ERROR,
               std::move(err));
      return true;
    }
    const std::string id = NewId();
    const std::string name = name_body && !name_body->empty()
                                 ? *name_body
                                 : DefaultAppName(*prompt);
    base::FilePath app_dir = AppsRootDir().AppendASCII(id);
    base::CreateDirectory(app_dir);
    const std::string readme =
        "# " + name + "\n\nBuilt with Grok Build in Xplorer.\n";
    base::WriteFile(app_dir.AppendASCII("README.md"), readme);
    const std::string conv_id = CreateAppConversation(id, name);
    base::DictValue app;
    app.Set("id", id);
    app.Set("name", name);
    app.Set("path", app_dir.value());
    app.Set("conversation_id", conv_id);
    app.Set("session_id", "");
    app.Set("status", kStatusIdle);
    app.Set("imported", false);
    app.Set("created_at", NowIso());
    app.Set("updated_at", NowIso());
    app.Set("last_error", "");
    base::DictValue registry = LoadRegistry();
    AppsList(registry)->Append(app.Clone());
    if (base::DictValue* stored = FindAppDict(registry, id))
      GetOrAssignRuntimePort(registry, stored);
    SaveRegistry(registry);
    base::DictValue result;
    result.Set("ok", true);
    if (base::DictValue* stored = FindAppDict(registry, id))
      result.Set("app", AppToJson(*stored, gateway_port));
    else
      result.Set("app", AppToJson(app, gateway_port));
    result.Set("build_stream_path",
                "/api/apps/" + id + "/build/stream");
    SendJson(server, connection_id, net::HTTP_OK, std::move(result));
    return true;
  }

  if (!base::StartsWith(path, prefix))
    return false;

  std::string rest = path.substr(prefix.size());
  const size_t slash = rest.find('/');
  const std::string app_id = slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string sub = slash == std::string::npos ? "" : rest.substr(slash + 1);

  if (app_id.empty()) {
    base::DictValue err;
    err.Set("error", "app id required");
    SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
    return true;
  }

  base::DictValue registry = LoadRegistry();
  base::DictValue* app = FindAppDict(registry, app_id);
  if (!app) {
    base::DictValue err;
    err.Set("error", "app not found");
    SendJson(server, connection_id, net::HTTP_NOT_FOUND, std::move(err));
    return true;
  }

  if (info.method == "GET" && sub.empty()) {
    EnsureAppRuntime(registry, app);
    base::DictValue result;
    result.Set("ok", true);
    result.Set("app", AppToJson(*app, gateway_port));
    SendJson(server, connection_id, net::HTTP_OK, std::move(result));
    return true;
  }

  if (info.method == "GET" && sub == "icon") {
    base::FilePath app_path = ResolveAppPath(*app);
    base::FilePath icon = app_path.AppendASCII("icon.png");
    if (!base::PathExists(icon))
      icon = app_path.AppendASCII("icon.svg");
    if (!base::PathExists(icon)) {
      const std::string* name = app->FindString("name");
      char letter = name && !name->empty() ? (*name)[0] : 'A';
      std::string svg = base::StringPrintf(
          R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="#1a73e8"/><text x="50%%" y="54%%" dominant-baseline="middle" text-anchor="middle" fill="white" font-family="system-ui,sans-serif" font-size="28" font-weight="600">%c</text></svg>)",
          letter);
      SendBytes(server, connection_id, net::HTTP_OK, std::move(svg),
                "image/svg+xml");
      return true;
    }
    std::string bytes;
    base::ReadFileToString(icon, &bytes);
    SendBytes(server, connection_id, net::HTTP_OK, std::move(bytes),
              GuessContentType(icon).c_str());
    return true;
  }

  if (info.method == "GET" && base::StartsWith(sub, "preview/")) {
    std::string rel = sub.substr(std::string("preview/").size());
    if (rel.empty())
      rel = "index.html";
    base::FilePath app_path = ResolveAppPath(*app);
    base::FilePath target = app_path.AppendASCII(rel);
    if (!IsPathInside(app_path, target) || !base::PathExists(target)) {
      base::DictValue err;
      err.Set("error", "file not found");
      SendJson(server, connection_id, net::HTTP_NOT_FOUND, std::move(err));
      return true;
    }
    std::string bytes;
    base::ReadFileToString(target, &bytes);
    SendBytes(server, connection_id, net::HTTP_OK, std::move(bytes),
              GuessContentType(target).c_str());
    return true;
  }

  if (info.method == "POST" &&
      (sub == "build/stream" || sub == "message/stream")) {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* prompt = body ? body->FindString("prompt") : nullptr;
    const std::string* message = body ? body->FindString("message") : nullptr;
    const std::string* model_body = body ? body->FindString("model") : nullptr;
    std::string text = message ? *message : (prompt ? *prompt : "");
    if (text.empty()) {
      base::DictValue err;
      err.Set("error", "prompt or message required");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    std::string conv_id;
    if (const std::string* cid = app->FindString("conversation_id"))
      conv_id = *cid;
    std::string session_id;
    if (const std::string* sid = app->FindString("session_id"))
      session_id = *sid;
    std::string model = ResolveAppBuildModel(model_body);
    base::FilePath cwd = ResolveAppPath(*app);
    MarkAppBuilding(app_id);
    AppendUserMessage(conv_id, text);
    RunGrokAgentStream(server, io_task_runner, connection_id, conv_id, app_id,
                       text, session_id, model, cwd);
    return true;
  }

  if (info.method == "POST" && sub == "rename") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* name = body ? body->FindString("name") : nullptr;
    if (!name || name->empty()) {
      base::DictValue err;
      err.Set("error", "name required");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    app->Set("name", *name);
    app->Set("updated_at", NowIso());
    SaveRegistry(registry);
    base::DictValue result;
    result.Set("ok", true);
    result.Set("app", AppToJson(*app, gateway_port));
    SendJson(server, connection_id, net::HTTP_OK, std::move(result));
    return true;
  }

  if (info.method == "POST" && sub == "duplicate") {
    base::FilePath src = ResolveAppPath(*app);
    if (src.empty() || !base::DirectoryExists(src)) {
      base::DictValue err;
      err.Set("error", "app folder missing");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    const std::string new_id = NewId();
    base::FilePath dst = AppsRootDir().AppendASCII(new_id);
    if (!base::CopyDirectory(src, dst, true)) {
      base::DictValue err;
      err.Set("error", "could not copy app folder");
      SendJson(server, connection_id, net::HTTP_INTERNAL_SERVER_ERROR,
               std::move(err));
      return true;
    }
    const std::string* old_name = app->FindString("name");
    std::string name =
        old_name && !old_name->empty() ? *old_name + " (copy)" : "App copy";
    const std::string conv_id = CreateAppConversation(new_id, name);
    base::DictValue dup;
    dup.Set("id", new_id);
    dup.Set("name", name);
    dup.Set("path", dst.value());
    dup.Set("conversation_id", conv_id);
    dup.Set("session_id", "");
    dup.Set("status", kStatusReady);
    dup.Set("imported", false);
    dup.Set("created_at", NowIso());
    dup.Set("updated_at", NowIso());
    dup.Set("last_error", "");
    AppsList(registry)->Append(dup.Clone());
    if (base::DictValue* stored = FindAppDict(registry, new_id))
      GetOrAssignRuntimePort(registry, stored);
    SaveRegistry(registry);
    base::DictValue result;
    result.Set("ok", true);
    if (base::DictValue* stored = FindAppDict(registry, new_id))
      result.Set("app", AppToJson(*stored, gateway_port));
    else
      result.Set("app", AppToJson(dup, gateway_port));
    SendJson(server, connection_id, net::HTTP_OK, std::move(result));
    return true;
  }

  if (info.method == "DELETE" && sub.empty()) {
    StopAppRuntimeServer(app_id);
    g_active_app_builds->erase(app_id);
    base::ListValue* apps = AppsList(registry);
    base::ListValue filtered;
    for (auto& v : *apps) {
      if (!v.is_dict())
        continue;
      const std::string* aid = v.GetDict().FindString("id");
      if (aid && *aid == app_id)
        continue;
      filtered.Append(v.Clone());
    }
    registry.Set("apps", std::move(filtered));
    SaveRegistry(registry);
    if (!app->FindBool("imported").value_or(false)) {
      base::FilePath app_path = ResolveAppPath(*app);
      if (!app_path.empty())
        base::DeletePathRecursively(app_path);
    }
    base::DictValue result;
    result.Set("ok", true);
    SendJson(server, connection_id, net::HTTP_OK, std::move(result));
    return true;
  }

  return false;
}

}  // namespace agent_gateway