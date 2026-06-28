// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/update_checker.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/version.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_thread.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/agent_gateway/xplorer_paths.h"
#include "crypto/secure_hash.h"
#include "crypto/sha2.h"
#endif

namespace agent_gateway {

namespace {

// The release feed: the latest published GitHub release for the fork. Its
// "assets" list carries the per-OS installers and (on recent releases) a
// "digest" of the form "sha256:<hex>".
constexpr char kLatestReleaseUrl[] =
    "https://api.github.com/repos/daniel-farina/xplorer/releases/latest";

// Cap on the release JSON body. The metadata is small; 256 KiB is generous.
constexpr size_t kMaxReleaseBody = 256 * 1024;

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
// Cap on a downloaded installer/tarball (1 GiB — installers are well under it).
constexpr int64_t kMaxArtifactBytes = 1024LL * 1024 * 1024;
#endif

// "mac" / "win" / "linux" — the os field reported in StatusDict and used to
// pick the matching release asset.
const char* RunningOs() {
#if BUILDFLAG(IS_MAC)
  return "mac";
#elif BUILDFLAG(IS_WIN)
  return "win";
#elif BUILDFLAG(IS_LINUX)
  return "linux";
#else
  return "other";
#endif
}

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
// Streaming SHA-256 of a file, returned as lowercase hex (matching GitHub's
// "sha256:<hex>" digest). Empty on any read error. Runs on a MayBlock thread.
std::string Sha256HexOfFile(const base::FilePath& path) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid())
    return std::string();
  std::unique_ptr<crypto::SecureHash> hash =
      crypto::SecureHash::Create(crypto::SecureHash::SHA256);
  std::vector<uint8_t> buffer(64 * 1024);
  for (;;) {
    std::optional<size_t> read = file.ReadAtCurrentPos(base::span(buffer));
    if (!read.has_value())
      return std::string();  // read error
    if (*read == 0)
      break;  // EOF
    hash->Update(base::span(buffer).first(*read));
  }
  std::array<uint8_t, crypto::kSHA256Length> digest;
  hash->Finish(base::span(digest));
  return base::HexEncodeLower(base::span(digest));
}

// Refuse any artifact URL that is not HTTPS under github.com /
// githubusercontent.com (the only hosts GitHub release assets live on).
bool IsAllowedHost(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIs("https"))
    return false;
  const std::string host = url.host();
  auto host_is = [&](const char* domain) {
    const std::string suffix = std::string(".") + domain;
    return host == domain || base::EndsWith(host, suffix);
  };
  return host_is("github.com") || host_is("githubusercontent.com");
}
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)

}  // namespace

// static
UpdateChecker* UpdateChecker::Get() {
  static base::NoDestructor<UpdateChecker> instance;
  return instance.get();
}

UpdateChecker::UpdateChecker() {
  status_.os = RunningOs();
}
UpdateChecker::~UpdateChecker() = default;

void UpdateChecker::Start() {
  if (started_)
    return;
  started_ = true;
  // The timer + the delayed first check live on this sequence (the gateway IO
  // thread). Record it so the delayed first check is posted back here.
  task_runner_ = base::SequencedTaskRunner::GetCurrentDefault();
  // base::Unretained is safe everywhere in this class: UpdateChecker is a
  // process-lifetime NoDestructor singleton, so it outlives every posted task.
  timer_.Start(FROM_HERE, kCheckInterval,
               base::BindRepeating(&UpdateChecker::OnTimer,
                                   base::Unretained(this)));
  // First check after a short delay so the HTTPS GET does not compete with cold
  // start (RepeatingTimer's own first tick is a full kCheckInterval out).
  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&UpdateChecker::OnTimer, base::Unretained(this)),
      kFirstCheckDelay);
}

void UpdateChecker::Stop() {
  // Cancel on the sequence that armed it (gateway IO thread). Stop() is a no-op
  // if never started, so this is safe whether or not Start() ran.
  timer_.Stop();
}

void UpdateChecker::OnTimer() {
  // The fetch (SimpleURLLoader + g_browser_process->shared_url_loader_factory())
  // is UI/network-service affine, so hop to the UI thread. Unretained: immortal
  // singleton.
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&UpdateChecker::FetchOnUI, base::Unretained(this)));
}

void UpdateChecker::FetchOnUI() {
  {
    base::AutoLock l(lock_);
    // Don't disturb an in-flight apply or a pending restart with a periodic
    // check (it would clobber the download/restart state).
    if (status_.state == State::kDownloading ||
        status_.state == State::kInstalling ||
        status_.state == State::kNeedsRestart) {
      return;
    }
    status_.state = State::kChecking;
  }

  scoped_refptr<network::SharedURLLoaderFactory> factory =
      g_browser_process->shared_url_loader_factory();
  if (!factory) {
    SetError("network service unavailable");
    return;
  }

  constexpr net::NetworkTrafficAnnotationTag kAnnotation =
      net::DefineNetworkTrafficAnnotation("xplorer_update_check", R"(
        semantics {
          sender: "Xplorer Updater"
          description:
            "Checks the Xplorer GitHub Releases feed for a newer build so the "
            "browser can offer an in-app update. Reads release metadata only "
            "(no installer download at this step)."
          trigger:
            "Runs ~30 seconds after the browser starts and every 6 hours "
            "thereafter."
          data: "None. A plain GET; no user data is sent."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "This request cannot be disabled in settings."
          policy_exception_justification:
            "Not implemented; this is the browser's own update channel."
        })");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(kLatestReleaseUrl);
  request->headers.SetHeader("Accept", "application/vnd.github+json");
  request->headers.SetHeader("User-Agent", "Xplorer-Updater");
  check_loader_ = network::SimpleURLLoader::Create(std::move(request),
                                                   kAnnotation);
  // GetWeakPtr() is first used here, on the UI thread, so weak_factory_ binds to
  // the UI sequence (the only sequence the async callbacks run on).
  check_loader_->DownloadToString(
      factory.get(),
      base::BindOnce(&UpdateChecker::OnBody, weak_factory_.GetWeakPtr()),
      kMaxReleaseBody);
}

void UpdateChecker::OnBody(std::optional<std::string> body) {
  check_loader_.reset();
  if (!body.has_value()) {
    // Non-2xx or network error. Keep the last-good cache; just flag the error.
    SetError("update check failed (network)");
    return;
  }
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  if (!parsed) {
    SetError("update check failed (parse)");
    return;
  }
  const std::string* tag = parsed->FindString("tag_name");
  if (!tag) {
    SetError("update check failed (no tag)");
    return;
  }

  // VERSION COMPARE. Tags are "v0.MIN.PAT" (or "0.MIN.PAT"). We assume MAJOR==0
  // and collapse MINOR/PATCH into a single integer the SAME way chrome/VERSION's
  // PATCH already encodes it: patch_code = MIN*100 + PAT (e.g. 0.8.7 -> 807).
  std::string ver = *tag;
  if (!ver.empty() && (ver.front() == 'v' || ver.front() == 'V'))
    ver = ver.substr(1);
  std::vector<std::string> parts = base::SplitString(
      ver, ".", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  int latest_code = -1;
  if (parts.size() >= 3) {
    int minor = 0;
    int patch = 0;
    if (base::StringToInt(parts[1], &minor) &&
        base::StringToInt(parts[2], &patch)) {
      latest_code = minor * 100 + patch;
    }
  }
  if (latest_code < 0) {
    SetError("update check failed (bad tag)");
    return;
  }

  // current_code = chrome/VERSION PATCH, read from the value already baked into
  // the binary (components() = [MAJOR, MINOR, BUILD, PATCH]); PATCH is index 3.
  // This reuses the running version rather than re-parsing it. (MAJOR==0.)
  const std::vector<uint32_t>& comps = version_info::GetVersion().components();
  const int current_code =
      comps.size() >= 4 ? static_cast<int>(comps[3]) : 0;

  // Pick the asset whose name matches the running OS.
  std::string asset_url;
  std::string asset_digest;
  int64_t asset_size = 0;
  if (const base::ListValue* assets = parsed->FindList("assets")) {
    for (const base::Value& value : *assets) {
      if (!value.is_dict())
        continue;
      const base::DictValue& asset = value.GetDict();
      const std::string* name = asset.FindString("name");
      const std::string* download_url =
          asset.FindString("browser_download_url");
      if (!name || !download_url)
        continue;
      bool matches = false;
#if BUILDFLAG(IS_MAC)
      matches = name->find("-macos-") != std::string::npos &&
                base::EndsWith(*name, ".dmg");
#elif BUILDFLAG(IS_WIN)
      matches = name->find("-windows-x64-installer-") != std::string::npos &&
                base::EndsWith(*name, ".exe");
#elif BUILDFLAG(IS_LINUX)
      matches = base::EndsWith(*name, ".tar.gz");
#endif
      if (!matches)
        continue;
      asset_url = *download_url;
      if (std::optional<double> size_d = asset.FindDouble("size"))
        asset_size = static_cast<int64_t>(*size_d);
      else if (std::optional<int> size_i = asset.FindInt("size"))
        asset_size = *size_i;
      // "digest" is "sha256:<hex>" and may be absent on old releases (then the
      // hash stays empty; on Windows Apply() refuses to install without one).
      if (const std::string* digest = asset.FindString("digest")) {
        constexpr char kPrefix[] = "sha256:";
        if (base::StartsWith(*digest, kPrefix))
          asset_digest = digest->substr(sizeof(kPrefix) - 1);
      }
      break;
    }
  }

  // current human string reconstructed from the PATCH code (MAJOR==0), so it is
  // directly comparable to |ver|: e.g. 806 -> "0.8.6".
  const std::string current_str =
      base::StringPrintf("0.%d.%d", current_code / 100, current_code % 100);

  base::AutoLock l(lock_);
  status_.current = current_str;
  status_.latest = ver;
  status_.url = asset_url;
  status_.sha256 = asset_digest;
  status_.size = asset_size;
  status_.available = (latest_code > current_code) && !asset_url.empty();
  status_.error.clear();
  status_.state = status_.available ? State::kAvailable : State::kIdle;
}

void UpdateChecker::SetState(State s) {
  base::AutoLock l(lock_);
  status_.state = s;
  if (s == State::kDownloading)
    status_.progress = 0;
}

void UpdateChecker::SetError(const std::string& message) {
  base::AutoLock l(lock_);
  status_.state = State::kError;
  status_.error = message;
}

// static
const char* UpdateChecker::StateName(State s) {
  switch (s) {
    case State::kIdle:
      return "idle";
    case State::kChecking:
      return "checking";
    case State::kAvailable:
      return "available";
    case State::kDownloading:
      return "downloading";
    case State::kInstalling:
      return "installing";
    case State::kNeedsRestart:
      return "needs_restart";
    case State::kError:
      return "error";
  }
  return "idle";
}

base::DictValue UpdateChecker::StatusDict() {
  base::AutoLock l(lock_);
  base::DictValue d;
  d.Set("available", status_.available);
  d.Set("state", StateName(status_.state));
  d.Set("current", status_.current);
  d.Set("latest", status_.latest);
  d.Set("os", status_.os);
  d.Set("url", status_.url);
  // Bytes can exceed base::Value's 32-bit int range across platforms, so emit a
  // double (lossless for installer sizes).
  d.Set("size", static_cast<double>(status_.size));
  d.Set("sha256", status_.sha256);
  d.Set("error", status_.error);
  d.Set("progress", status_.progress);
  d.Set("installed_path", status_.installed_path);
  return d;
}

base::DictValue UpdateChecker::Apply() {
  base::DictValue d;
#if BUILDFLAG(IS_MAC)
  // macOS updates ship through Sparkle, owned by another agent. No-op here.
  base::AutoLock l(lock_);
  d.Set("ok", false);
  d.Set("state", StateName(status_.state));
  d.Set("note", "macOS uses Sparkle");
  return d;
#else
  std::string url;
  std::string sha256;
  bool available = false;
  State state = State::kIdle;
  {
    base::AutoLock l(lock_);
    url = status_.url;
    sha256 = status_.sha256;
    available = status_.available;
    state = status_.state;
  }

  // Already working on it.
  if (state == State::kDownloading || state == State::kInstalling) {
    d.Set("ok", true);
    d.Set("state", StateName(state));
    return d;
  }
  if (!available || url.empty()) {
    d.Set("ok", false);
    d.Set("state", StateName(state));
    d.Set("note", "no update available");
    return d;
  }
  if (!IsAllowedHost(GURL(url))) {
    SetError("refusing download from untrusted host");
    d.Set("ok", false);
    d.Set("state", StateName(State::kError));
    return d;
  }
#if BUILDFLAG(IS_WIN)
  // Fail closed: never run a Windows installer we can't verify.
  if (sha256.empty()) {
    SetError("refusing install without a checksum");
    d.Set("ok", false);
    d.Set("state", StateName(State::kError));
    return d;
  }
#endif

  SetState(State::kDownloading);
  // Hop to the UI thread to run the download/verify/install. Unretained:
  // immortal singleton. The url/sha256 ride along so StartDownloadOnUI never
  // reads status_ off-sequence.
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&UpdateChecker::StartDownloadOnUI,
                                base::Unretained(this), url, sha256));
  d.Set("ok", true);
  d.Set("state", StateName(State::kDownloading));
  return d;
#endif  // BUILDFLAG(IS_MAC)
}

base::DictValue UpdateChecker::Restart() {
  // chrome::AttemptRelaunch() must run on the UI thread.
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce([]() { chrome::AttemptRelaunch(); }));
  base::DictValue d;
  d.Set("ok", true);
  return d;
}

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)

struct UpdateChecker::InstallOutcome {
  bool ok = false;
  std::string error;
  std::string installed_path;  // Linux: the kept tarball; Windows: empty.
};

void UpdateChecker::StartDownloadOnUI(std::string url,
                                      std::string expected_sha256) {
  scoped_refptr<network::SharedURLLoaderFactory> factory =
      g_browser_process->shared_url_loader_factory();
  if (!factory) {
    SetError("network service unavailable");
    return;
  }

  constexpr net::NetworkTrafficAnnotationTag kAnnotation =
      net::DefineNetworkTrafficAnnotation("xplorer_update_download", R"(
        semantics {
          sender: "Xplorer Updater"
          description:
            "Downloads the Xplorer installer (Windows) or update archive "
            "(Linux) for a newer release the user chose to install. The "
            "downloaded file is verified against its published SHA-256 digest "
            "before it is run or kept."
          trigger:
            "The user accepts the in-app update prompt (POST /api/update/apply)."
          data: "None sent. The response is the installer/archive."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "This request cannot be disabled in settings."
          policy_exception_justification:
            "Not implemented; this is the browser's own update channel."
        })");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(url);
  request->headers.SetHeader("User-Agent", "Xplorer-Updater");
  download_loader_ = network::SimpleURLLoader::Create(std::move(request),
                                                      kAnnotation);
  download_loader_->SetOnDownloadProgressCallback(base::BindRepeating(
      &UpdateChecker::OnDownloadProgress, weak_factory_.GetWeakPtr()));
  download_loader_->DownloadToTempFile(
      factory.get(),
      base::BindOnce(&UpdateChecker::OnDownloaded, weak_factory_.GetWeakPtr(),
                     expected_sha256),
      kMaxArtifactBytes);
}

void UpdateChecker::OnDownloadProgress(uint64_t current) {
  base::AutoLock l(lock_);
  if (status_.size > 0) {
    int pct = static_cast<int>((static_cast<int64_t>(current) * 100) /
                               status_.size);
    pct = std::min(100, std::max(0, pct));
    status_.progress = pct;
  }
}

void UpdateChecker::OnDownloaded(std::string expected_sha256,
                                 base::FilePath path) {
  download_loader_.reset();
  if (path.empty()) {
    SetError("download failed");
    return;
  }
  std::string url;
  {
    base::AutoLock l(lock_);
    status_.progress = 100;
    status_.state = State::kInstalling;
    url = status_.url;
  }
  // Verify (hash) + install on a MayBlock pool thread (file I/O + process
  // launch), then apply the outcome back on the UI thread. CONTINUE_ON_SHUTDOWN:
  // a verify/install in flight must not block a browser exit.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&UpdateChecker::VerifyAndInstall, path,
                     std::move(expected_sha256), url),
      base::BindOnce(&UpdateChecker::OnInstallDone,
                     weak_factory_.GetWeakPtr()));
}

// static
UpdateChecker::InstallOutcome UpdateChecker::VerifyAndInstall(
    base::FilePath downloaded,
    std::string expected_sha256,
    std::string url) {
  InstallOutcome out;
  const std::string got = Sha256HexOfFile(downloaded);

#if BUILDFLAG(IS_WIN)
  // Windows fails closed: a checksum is required and must match.
  if (expected_sha256.empty() || got.empty() ||
      !base::EqualsCaseInsensitiveASCII(got, expected_sha256)) {
    out.error = "checksum verification failed";
    return out;
  }
  // It's a real .exe — launch it directly (no shell wrap). --do-not-launch-chrome
  // installs in the background; the user restarts to finish.
  base::CommandLine cmd(downloaded);
  cmd.AppendArg("--do-not-launch-chrome");
  base::LaunchOptions options;
  options.start_hidden = true;
  base::Process process = base::LaunchProcess(cmd, options);
  if (!process.IsValid()) {
    out.error = "failed to launch installer";
    return out;
  }
  out.ok = true;
  return out;
#elif BUILDFLAG(IS_LINUX)
  // Linux digests may be absent on older releases; if one is present it must
  // match. (We never auto-extract over a running install — we just keep the
  // verified tarball under ~/.xplorer/updates for the banner to surface.)
  if (!expected_sha256.empty() &&
      (got.empty() ||
       !base::EqualsCaseInsensitiveASCII(got, expected_sha256))) {
    out.error = "checksum verification failed";
    return out;
  }
  base::FilePath dir = xplorer_paths::Resolve("updates");
  if (dir.empty()) {
    out.error = "cannot resolve updates directory";
    return out;
  }
  base::CreateDirectory(dir);
  std::string filename = GURL(url).ExtractFileName();
  if (filename.empty())
    filename = "xplorer-update.tar.gz";
  base::FilePath dest = dir.AppendASCII(filename);
  if (!base::Move(downloaded, dest)) {
    out.error = "failed to store update archive";
    return out;
  }
  out.ok = true;
  out.installed_path = dest.AsUTF8Unsafe();
  return out;
#else
  out.error = "unsupported platform";
  return out;
#endif
}

void UpdateChecker::OnInstallDone(InstallOutcome outcome) {
  if (!outcome.ok) {
    SetError(outcome.error.empty() ? "install failed" : outcome.error);
    return;
  }
  base::AutoLock l(lock_);
  status_.state = State::kNeedsRestart;
  status_.progress = 100;
  status_.error.clear();
  status_.installed_path = outcome.installed_path;
}

#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)

}  // namespace agent_gateway
