// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_SCHEDULER_H_
#define CHROME_BROWSER_AGENT_GATEWAY_SCHEDULER_H_

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"

namespace agent_gateway {

// In-process background-task scheduler. Owned by AgentGateway (process-lifetime
// singleton), armed on the gateway IO thread (server_thread_). It polls every
// ~30s, compares base::Time::Now() to each enabled job's next_fire_us, and
// dispatches matches headlessly (no live HTTP connection) by reusing the grok
// run machinery in grok_native.cc.
//
// Phase 2 supports two trigger kinds: a fixed interval (interval_sec) and a
// one-shot epoch time (once_at_us). Cron expressions are stored but not yet
// evaluated (Phase 3).
//
// Persistence mirrors companion_sessions.json: the job list is loaded from /
// saved to schedules.json via xplorer_paths::Resolve, using base::ReadFileToString
// + base::JSONReader + base::JSONWriter::Write + base::WriteFile.
//
// Threading: the timer fires on the gateway IO thread. Job mutations (firing,
// recomputing next_fire_us, persistence) happen on that thread; the actual run
// is handed to a base::ThreadPool task (it blocks on the grok subprocess) which
// hops back to the UI thread only as the run machinery itself requires.
class Scheduler {
 public:
  // A single scheduled job (data model, design §4.1). All times are epoch
  // microseconds (base::Time::ToDeltaSinceWindowsEpoch().InMicroseconds()).
  struct Job {
    std::string id;
    std::string label;
    bool enabled = true;

    // Trigger: exactly one of the three should be meaningful. interval_sec > 0
    // is a repeating interval; once_at_us > 0 is a one-shot; cron is stored but
    // not evaluated in Phase 2.
    std::string cron;             // stored, not yet evaluated (Phase 3)
    int interval_sec = 0;         // 0 == unset
    int64_t once_at_us = 0;       // 0 == unset

    int64_t next_fire_us = 0;
    int64_t last_fire_us = 0;
    std::string last_status;      // ok | failed | running | skipped | deferred

    // Run definition.
    std::string message;
    std::string model;
    std::string target_conv_id;   // where replies are appended; auto-created if empty

    // App-build runs. A non-empty |cwd| switches the fire from a plain chat run
    // (DispatchScheduledRun) to a headless app-build run (DispatchScheduledAppBuild),
    // which invokes grok with --cwd <cwd> + the app-build rules — the same
    // command POST /apps/{id}/build/stream runs, but blocking and headless.
    // |app_id| is informational (which app this job builds); the run keys off cwd.
    std::string cwd;              // empty == plain chat run; non-empty == app-build
    std::string app_id;           // informational; the app this build targets

    // A "browse" task needs no separate code path: it is simply a chat Job whose
    // |model| is browser-capable. A scheduled chat run with such a model spawns
    // background tabs through the normal POST /tabs path automatically; this knob
    // caps how many open at once.
    int max_concurrent_tabs = 5;
  };

  static Scheduler* Get();

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  // Loads schedules.json into the in-memory job list and starts the poll timer.
  // Must be called on the thread that owns the timer (the gateway IO thread).
  // Idempotent: a second call reloads jobs without double-arming.
  void Start();

  // --- CRUD (called from the GET/POST/DELETE /api/schedules handlers). The
  // scheduler keeps the canonical in-memory list; these mutate it + persist. ---

  // All jobs as a {"version":1,"jobs":[...]} dict, for GET /api/schedules.
  base::DictValue ListJobsDict() const;

  // Thread-safe snapshot of the job list for UI consumers (the native
  // "Scheduled" sidebar section runs on the UI thread and must NOT read jobs_
  // directly — jobs_ lives on the gateway IO thread, task_runner_). Computes
  // ListJobsDict() on task_runner_ and runs |callback| with the result on the
  // caller's sequence. If the scheduler has not been Start()ed yet (task_runner_
  // is null), replies with an empty {"version":1,"jobs":[]} dict on the caller's
  // sequence.
  void GetJobsAsync(base::OnceCallback<void(base::DictValue)> callback);

  // Create or update a job from a JSON body (POST /api/schedules). If the body
  // carries an existing id the matching job is replaced; otherwise a new id is
  // minted. Computes the initial next_fire_us and persists. Returns the stored
  // job as a dict (or {"error":...} on a malformed body).
  base::DictValue UpsertJobFromDict(const base::DictValue& body);

  // Remove the job with |id| and persist. Returns true if a job was removed.
  bool RemoveJob(const std::string& id);

 private:
  friend class base::NoDestructor<Scheduler>;

  Scheduler();
  ~Scheduler();

  // Timer tick: scan jobs, dispatch any whose next_fire_us has arrived.
  void Poll();

  // Fire one job: hand its run to the headless dispatch helper and recompute
  // next_fire_us (interval -> now+interval; once -> disable). Runs on the timer
  // thread.
  void FireJob(Job* job);

  // (Re)compute next_fire_us for |job| from its trigger, relative to |now|.
  // interval: now + interval_sec; once: once_at_us (or disable if in the past
  // already-fired); cron: left at 0 in Phase 2.
  void ComputeNextFire(Job* job, base::Time now);

  // Persistence (mirrors LoadSessions/SaveSessions in grok_native.cc).
  void Load();
  void Save() const;

  // Records the result of a fired run back onto the job (last_status, and the
  // auto-created conv id for future fires) + persists. MUST run on the
  // scheduler's own task runner (the gateway IO thread), since it mutates jobs_.
  void OnRunComplete(const std::string& job_id,
                     const std::string& original_target_conv_id,
                     const std::string& status,
                     const std::string& conv_id);

  // Serialize/deserialize a single job.
  static base::DictValue JobToDict(const Job& job);
  static Job JobFromDict(const base::DictValue& d);

  std::vector<Job> jobs_;
  base::RepeatingTimer timer_;
  bool started_ = false;
  // The sequence that owns jobs_ + the timer (the gateway IO thread). Captured
  // in Start(); used to hop async run-completion callbacks back onto it.
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_SCHEDULER_H_
