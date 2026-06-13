// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/grok_native.h"

#include <unistd.h>

#include <map>
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
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/agent_gateway/browser_api.h"
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
      if (base::ReadFileToString(home.AppendASCII(".aether/companion.json"),
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

base::FilePath UiDir() {
  const char* env = getenv("XBROWSER_COMPANION_UI");
  if (env && *env)
    return base::FilePath(env);
  base::FilePath home;
  if (base::PathService::Get(base::DIR_HOME, &home)) {
    base::FilePath candidate =
        home.AppendASCII("cli_experiment/aether/companion/ui");
    if (base::DirectoryExists(candidate))
      return candidate;
  }
  return base::FilePath();
}

base::FilePath SessionsFile() {
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home))
    return base::FilePath();
  return home.AppendASCII(".aether/companion_sessions.json");
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

base::FilePath SettingsFile() {
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home))
    return base::FilePath();
  return home.AppendASCII(".aether/grok_settings.json");
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
  std::string model = ResolveModel(request_model);
  if (SearchModeNeedsWebTools(mode) && model == kComposerModel)
    return kSearchModel;
  if (SearchModeNeedsWebTools(mode) && model == kDefaultModel)
    return kSearchModel;
  return model;
}

std::string ModelDisplayName(const std::string& model) {
  if (model == "grok-composer-2.5-fast")
    return "Composer 2.5";
  if (model == "grok-build")
    return "Grok Build";
  return model;
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

const char* SearchPromptForMode(const std::string& mode, bool has_image) {
  if (mode == "images" && has_image) {
    return "You are Grok Vision Search for XBrowser. Analyze the attached "
           "image. The user may want a description, text/OCR extraction, "
           "explanation of what is shown, or help finding similar images "
           "online — use web search when useful. Be specific. End with JSON: "
           "{\"answer\":\"...\",\"links\":[{\"title\":\"...\",\"url\":"
           "\"https://...\",\"snippet\":\"...\"}],"
           "\"images\":[{\"url\":\"https://...\",\"title\":\"...\","
           "\"description\":\"...\"}]}";
  }
  if (mode == "images") {
    return "You are Grok Image Search for XBrowser. Describe and list image "
           "search results for the text query. End with JSON: "
           "{\"answer\":\"...\",\"links\":[{\"title\":\"...\",\"url\":"
           "\"https://...\",\"snippet\":\"...\"}],"
           "\"images\":[{\"url\":\"https://...\",\"title\":\"...\","
           "\"description\":\"...\"}]}";
  }
  if (mode == "videos") {
    return "You are Grok Video Search for XBrowser. Summarize video results "
           "with titles and URLs. End with JSON: "
           "{\"links\":[{\"title\":\"...\",\"url\":\"https://...\","
           "\"snippet\":\"...\"}]}";
  }
  if (mode == "imagine") {
    return "Generate an image for this prompt. Return a brief caption then "
           "any image URLs produced.";
  }
  return "You are Grok Search for XBrowser. Answer the web search query using "
         "current web knowledge. End with a JSON block: "
         "{\"links\":[{\"title\":\"...\",\"url\":\"https://...\","
         "\"snippet\":\"...\"}]} with up to 5 relevant links.";
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
  size_t json_start = text.rfind("{\"links\"");
  if (json_start == std::string::npos)
    json_start = text.rfind("{\"answer\"");
  if (json_start == std::string::npos)
    json_start = text.rfind("{\"images\"");
  if (json_start == std::string::npos)
    json_start = text.rfind("\n{");
  if (json_start != std::string::npos && text[json_start] == '\n')
    json_start++;
  std::string answer = text;
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
    if (auto link_json =
            base::JSONReader::ReadDict(text.substr(json_start),
                                       base::JSON_PARSE_RFC)) {
      if (const base::ListValue* list = link_json->FindList("links"))
        links = list->Clone();
      if (const base::ListValue* imgs = link_json->FindList("images"))
        images = imgs->Clone();
    }
  }
  base::DictValue result;
  result.Set("mode", mode);
  result.Set("answer", answer);
  result.Set("text", answer);
  result.Set("links", std::move(links));
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

enum class GrokStreamKind { kSearch, kChat };

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

void PumpGrokStream(net::HttpServer* server,
                    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
                    int connection_id,
                    base::CommandLine cmd,
                    std::string model,
                    std::string mode,
                    GrokStreamKind kind,
                    std::string conv_id) {
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    io_task_runner->PostTask(
        FROM_HERE, base::BindOnce(&SendStreamError, server, connection_id,
                                  "failed to create pipe"));
    return;
  }

  base::LaunchOptions options;
  // Only stream stdout (streaming-json). Merging stderr injects ANSI logs like
  // "[2m2026-..." that break NDJSON parsing in the companion UI.
  options.fds_to_remap.emplace_back(pipe_fds[1], STDOUT_FILENO);
  base::Process process = base::LaunchProcess(cmd, options);
  close(pipe_fds[1]);

  if (!process.IsValid()) {
    close(pipe_fds[0]);
    io_task_runner->PostTask(
        FROM_HERE,
        base::BindOnce(
            &SendStreamError, server, connection_id,
            "failed to launch grok at " + cmd.GetProgram().MaybeAsASCII() +
                " — run `grok login` or set GROK_BIN in ~/.aether/companion.json"));
    return;
  }

  io_task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&BeginNdjsonStreamWithMeta, server, connection_id, model,
                     kind == GrokStreamKind::kSearch ? mode : "chat"));

  base::File read_file(pipe_fds[0]);
  std::string buffer;
  std::string full_text;
  std::string session_id;
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
          if (const std::string* data = parsed->FindString("data"))
            full_text += *data;
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

  if (exit_code != 0) {
    base::DictValue err;
    err.Set("type", "error");
    err.Set("error", "grok failed (exit " + base::NumberToString(exit_code) +
                          ")");
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
    if (const base::ListValue* images = parsed.FindList("images"))
      result_event.Set("images", images->Clone());
  } else {
    result_event.Set("text", full_text);
    result_event.Set("reply", full_text);
    if (!session_id.empty())
      result_event.Set("sessionId", session_id);
    if (!conv_id.empty() && !full_text.empty())
      SaveChatAssistantReply(conv_id, full_text, session_id);
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
                     model, mode, GrokStreamKind::kSearch, ""));
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

base::CommandLine BuildGrokChatCommand(const std::string& message,
                                       const std::string& session_id,
                                       const std::string& model,
                                       bool streaming) {
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
  cmd.AppendArg("--rules");
  cmd.AppendArg(
      "You are Grok, the native AI companion built into XBrowser. You can "
      "control the browser through MCP tools.");
  if (!session_id.empty()) {
    cmd.AppendArg("-r");
    cmd.AppendArg(session_id);
  }
  return cmd;
}

void RunGrokChatStream(
    net::HttpServer* server,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    int connection_id,
    std::string conv_id,
    std::string message,
    std::string session_id,
    std::string model) {
  base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE})
      ->PostTask(FROM_HERE,
                 base::BindOnce(&PumpGrokStream, server, io_task_runner,
                                connection_id,
                                BuildGrokChatCommand(message, session_id, model,
                                                     true),
                                model, "chat", GrokStreamKind::kChat, conv_id));
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
  base::CommandLine cmd =
      BuildGrokChatCommand(message, session_id, model, false);
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

}  // namespace

bool GrokNative::TryHandleRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info,
    net::HttpServer* server,
    int gateway_port,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner) {
  const std::string path = PathOnly(info.path);

  if (info.method == "OPTIONS") {
    net::HttpServerResponseInfo resp(net::HTTP_NO_CONTENT);
    resp.AddHeader("Access-Control-Allow-Origin", "*");
    resp.AddHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    resp.AddHeader("Access-Control-Allow-Headers", "Content-Type");
    server->SendResponse(connection_id, resp, TRAFFIC_ANNOTATION_FOR_TESTS);
    return true;
  }

  // Static assets before /search page route.
  if (info.method == "GET" &&
      (base::EndsWith(path, ".css") || base::EndsWith(path, ".js"))) {
    return ServeUiFile(server, connection_id, path.substr(path.rfind('/') + 1));
  }

  if (info.method == "GET" && (path == "/" || path == "/index.html")) {
    if (WantsHtml(info))
      return ServeUiFile(server, connection_id, "index.html");
    return false;  // Agent discovery JSON handled by AgentGateway.
  }

  if (info.method == "GET" && (path == "/search" || path == "/search/")) {
    return ServeUiFile(server, connection_id, "search.html");
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

  // Browser theme for companion UI (no auth — same as other native UI routes).
  if (info.method == "GET" && (path == "/theme" || path == "/api/theme")) {
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
    SendJson(server, connection_id, net::HTTP_OK, std::move(d));
    return true;
  }

  if (info.method == "POST" && path == "/api/settings") {
    auto body = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const std::string* model = body ? body->FindString("model") : nullptr;
    if (!model || model->empty()) {
      base::DictValue err;
      err.Set("error", "missing model");
      SendJson(server, connection_id, net::HTTP_BAD_REQUEST, std::move(err));
      return true;
    }
    SetConfiguredModel(*model);
    base::DictValue d;
    d.Set("ok", true);
    d.Set("model", *model);
    d.Set("model_label", ModelDisplayName(*model));
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
    std::string model = ResolveModel(model_body);
    if (!message || message->empty()) {
      base::DictValue err;
      err.Set("error", "empty message");
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