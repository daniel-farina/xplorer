// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/app_store.h"

#include <map>
#include <set>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
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

base::NoDestructor<std::set<std::string>> g_active_app_builds;

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
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home))
    return base::FilePath();
  return home.AppendASCII(".aether/apps");
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

std::string AppStatus(const base::DictValue& app) {
  if (const std::string* id = app.FindString("id");
      id && g_active_app_builds->count(*id))
    return kStatusBuilding;
  const std::string* status = app.FindString("status");
  return status ? *status : kStatusIdle;
}

base::DictValue AppToJson(const base::DictValue& app) {
  base::DictValue out = app.Clone();
  out.Set("status", AppStatus(app));
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
    app->Set("last_error", "grok build failed");
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
    base::ListValue* apps = registry.FindList("apps");
    base::ListValue out_apps;
    if (apps) {
      for (const auto& v : *apps) {
        if (!v.is_dict())
          continue;
        out_apps.Append(AppToJson(v.GetDict()));
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
    SaveRegistry(registry);
    base::DictValue result;
    result.Set("ok", true);
    result.Set("app", AppToJson(app));
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
        "# " + name + "\n\nBuilt with Grok Build in XBrowser.\n";
    base::WriteFile(app_dir.AppendASCII("README.md"), readme);
    const std::string conv_id = CreateAppConversation(id, name);
    base::DictValue app;
    app.Set("id", id);
    app.Set("name", name);
    app.Set("path", app_dir.value());
    app.Set("conversation_id", conv_id);
    app.Set("session_id", "");
    app.Set("status", kStatusBuilding);
    app.Set("imported", false);
    app.Set("created_at", NowIso());
    app.Set("updated_at", NowIso());
    app.Set("last_error", "");
    base::DictValue registry = LoadRegistry();
    AppsList(registry)->Append(app.Clone());
    SaveRegistry(registry);
    MarkAppBuilding(id);
    AppendUserMessage(conv_id, *prompt);
    base::DictValue result;
    result.Set("ok", true);
    result.Set("app", AppToJson(app));
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
    base::DictValue result;
    result.Set("ok", true);
    result.Set("app", AppToJson(*app));
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
    std::string model = ResolveConfiguredModel(model_body);
    base::FilePath cwd = ResolveAppPath(*app);
    MarkAppBuilding(app_id);
    AppendUserMessage(conv_id, text);
    RunGrokAgentStream(server, io_task_runner, connection_id, conv_id, app_id,
                       text, session_id, model, cwd);
    return true;
  }

  if (info.method == "DELETE" && sub.empty()) {
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