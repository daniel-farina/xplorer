// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_UPDATE_CHECKER_H_
#define CHROME_BROWSER_AGENT_GATEWAY_UPDATE_CHECKER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "build/build_config.h"

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace agent_gateway {

// Process-lifetime singleton that periodically checks GitHub Releases for a
// newer Xplorer build and, on Windows/Linux, downloads + verifies + installs it.
//
// Modeled EXACTLY on Scheduler (scheduler.{h,cc}): a Get() NoDestructor
// singleton, a base::RepeatingTimer armed on the gateway IO thread in Start(),
// Stop() cancels it. The first check runs ~30s after Start() (PostDelayedTask)
// so it does not compete with cold start; thereafter every kCheckInterval.
//
// Threading: the timer fires on the gateway IO thread (task_runner_). The HTTPS
// fetch / download / install MUST run on the UI thread — SimpleURLLoader and
// g_browser_process->shared_url_loader_factory() are UI/network-service affine —
// so the timer tick hops through content::GetUIThreadTaskRunner(). The shared
// UpdateStatus struct is READ on the IO thread (StatusDict, for GET
// /api/update/status, and the Apply/Restart endpoints) and WRITTEN on the UI
// thread, so it is guarded by lock_ (mirrors the ActiveRuns lock in
// grok_native.cc).
class UpdateChecker {
 public:
  enum class State {
    kIdle,
    kChecking,
    kAvailable,
    kDownloading,
    kInstalling,
    kNeedsRestart,
    kError,
  };

  // Snapshot of what the updater knows. Guarded by lock_.
  struct UpdateStatus {
    State state = State::kIdle;
    bool available = false;
    std::string current;         // running version, e.g. "0.8.6"
    std::string latest;          // newest release tag w/o leading 'v', "0.8.7"
    std::string os;              // "mac" | "win" | "linux"
    std::string url;             // this-OS asset download URL
    std::string sha256;          // expected lowercase hex digest ("" if absent)
    std::string error;           // last error (kept across a good cache)
    std::string installed_path;  // verified artifact (Linux tarball) path
    int64_t size = 0;            // asset size in bytes
    int progress = 0;            // 0..100 during download
  };

  // Every 6 hours. The first check is a one-off PostDelayedTask (~30s).
  static constexpr base::TimeDelta kCheckInterval = base::Hours(6);
  static constexpr base::TimeDelta kFirstCheckDelay = base::Seconds(30);

  static UpdateChecker* Get();

  UpdateChecker(const UpdateChecker&) = delete;
  UpdateChecker& operator=(const UpdateChecker&) = delete;

  // Arms the poll timer on the calling sequence (the gateway IO thread) and
  // schedules the first check after kFirstCheckDelay. Idempotent. Must be
  // called on the thread that owns the timer (the gateway IO thread).
  void Start();

  // Cancels the poll timer so the gateway IO thread can be joined cleanly on
  // shutdown. MUST run on the sequence that called Start() (base::RepeatingTimer
  // is sequence-affine). Idempotent and safe if Start() never ran.
  void Stop();

  // Snapshot of the current status as a dict (GET /api/update/status). Safe on
  // the IO thread; takes lock_.
  base::DictValue StatusDict();

  // Begins the per-OS install of the currently-known update (POST
  // /api/update/apply). No-op on macOS (Sparkle owns it; another agent). On
  // Win/Linux hops to the UI thread to download/verify/install. Returns
  // {ok, state, note?}. Called on the IO thread.
  base::DictValue Apply();

  // Relaunches the browser to finish an applied update (POST
  // /api/update/restart). Only meaningful when state == needs_restart. Called on
  // the IO thread; hops to the UI thread for chrome::AttemptRelaunch().
  base::DictValue Restart();

  // --- Update controls for the Settings "Updates" pane (Win/Linux; the macOS
  // build routes the same UI to Sparkle instead). Called on the gateway IO
  // thread (the timer's sequence). ---

  // Whether scheduled auto-checks are enabled (persisted; default true).
  bool AutoCheckEnabled();
  // Enable/disable scheduled auto-checks: persists the preference and (re)arms or
  // cancels the poll timer. Manual CheckNow() still works when disabled.
  void SetAutoCheck(bool enabled);
  // Run a check immediately, regardless of the auto-check preference.
  void CheckNow();

 private:
  friend class base::NoDestructor<UpdateChecker>;
  UpdateChecker();
  ~UpdateChecker();

  // Timer tick (IO thread): hop to the UI thread to run the HTTPS check.
  void OnTimer();

  // UI thread: issue the GitHub releases/latest GET.
  void FetchOnUI();
  // UI thread: parse the release JSON, pick this OS's asset, update status_.
  void OnBody(std::optional<std::string> body);

  // Status mutation helpers (take lock_).
  void SetState(State s);
  void SetError(const std::string& message);
  static const char* StateName(State s);

  // Persisted auto-check preference (~/.xplorer/update_prefs.json). Read in
  // Start(); written by SetAutoCheck. Defaults to true when absent.
  bool ReadAutoCheckPref();
  void WriteAutoCheckPref(bool enabled);

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  // UI thread: begin the download of the chosen asset, then verify + install.
  void StartDownloadOnUI(std::string url, std::string expected_sha256);
  void OnDownloadProgress(uint64_t current);
  void OnDownloaded(std::string expected_sha256, base::FilePath path);
  // Result of the MayBlock verify+install task. Defined in the .cc.
  struct InstallOutcome;
  // MayBlock pool: hash |downloaded|, verify against |expected_sha256|, then
  // install (Windows: launch the verified installer; Linux: move the verified
  // tarball under ~/.xplorer/updates). Returns the outcome. A static member so
  // it can name the private InstallOutcome type and be passed to base::BindOnce.
  static InstallOutcome VerifyAndInstall(base::FilePath downloaded,
                                         std::string expected_sha256,
                                         std::string url);
  // UI thread: apply the result of VerifyAndInstall to status_.
  void OnInstallDone(InstallOutcome outcome);
#endif

  base::Lock lock_;
  UpdateStatus status_;  // guarded by lock_

  base::RepeatingTimer timer_;
  bool started_ = false;
  // Scheduled auto-check enabled (persisted). IO-thread-only (set in Start() /
  // SetAutoCheck, read in StatusDict / AutoCheckEnabled — all on the IO thread).
  bool auto_check_ = true;
  // The sequence that owns the timer (the gateway IO thread). Captured in
  // Start(); used to schedule the delayed first check.
  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // Live loaders, UI-thread only. Kept alive as members per the SimpleURLLoader
  // contract (destroying one cancels its request).
  std::unique_ptr<network::SimpleURLLoader> check_loader_;
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  std::unique_ptr<network::SimpleURLLoader> download_loader_;
#endif

  // Bound + dereferenced exclusively on the UI thread (the async fetch/download/
  // install callbacks). The IO-thread timer/Apply/Restart hops use Unretained
  // (the singleton is immortal), so the factory only ever binds to the UI thread.
  base::WeakPtrFactory<UpdateChecker> weak_factory_{this};
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_UPDATE_CHECKER_H_
