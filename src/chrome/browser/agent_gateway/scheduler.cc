// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/agent_gateway/scheduler.h"

#include <algorithm>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/agent_gateway/grok_native.h"
#include "chrome/browser/agent_gateway/xplorer_paths.h"

namespace agent_gateway {

namespace {

// How often the poll timer fires. Jobs fire when next_fire_us has passed at
// poll time, so the effective scheduling granularity is this interval.
constexpr base::TimeDelta kPollInterval = base::Seconds(30);

// Epoch microseconds for a base::Time, matching the design's *_us fields. We use
// the Windows epoch consistently (it is monotonic with wall clock and the same
// base used by the rest of the *_us fields in the data model).
int64_t ToEpochUs(base::Time t) {
  return t.ToDeltaSinceWindowsEpoch().InMicroseconds();
}

}  // namespace

// static
Scheduler* Scheduler::Get() {
  static base::NoDestructor<Scheduler> instance;
  return instance.get();
}

Scheduler::Scheduler() = default;
Scheduler::~Scheduler() = default;

void Scheduler::Start() {
  Load();
  if (!started_) {
    started_ = true;
    // jobs_ and the timer live on this sequence (the gateway IO thread). Record
    // it so async run-completion callbacks (which fire on a ThreadPool thread)
    // can hop back here before touching jobs_.
    task_runner_ = base::SequencedTaskRunner::GetCurrentDefault();
    timer_.Start(FROM_HERE, kPollInterval,
                 base::BindRepeating(&Scheduler::Poll, base::Unretained(this)));
  }
  // Fire once immediately so jobs whose time already passed (e.g. while the
  // browser was closed, or set to a near-past once_at) are not stranded until
  // the first 30s tick.
  Poll();
}

void Scheduler::Poll() {
  const base::Time now = base::Time::Now();
  const int64_t now_us = ToEpochUs(now);
  bool changed = false;
  for (Job& job : jobs_) {
    if (!job.enabled)
      continue;
    if (job.next_fire_us <= 0 || job.next_fire_us > now_us)
      continue;
    FireJob(&job);
    changed = true;
  }
  if (changed)
    Save();
}

void Scheduler::FireJob(Job* job) {
  const base::Time now = base::Time::Now();
  job->last_fire_us = ToEpochUs(now);
  job->last_status = "running";

  // Recompute the next fire BEFORE dispatching so a long-running grok run does
  // not delay the schedule. A one-shot disables itself after firing.
  if (job->interval_sec > 0) {
    ComputeNextFire(job, now);
  } else if (job->once_at_us > 0) {
    job->enabled = false;
    job->next_fire_us = 0;
  }

  const std::string job_id = job->id;
  const std::string original_target = job->target_conv_id;
  scoped_refptr<base::SequencedTaskRunner> runner = task_runner_;
  // The run is async and resolves on a ThreadPool thread; hop the result back
  // onto our own sequence before touching jobs_ (looked up by id, since jobs_
  // may be mutated by CRUD in the meantime). base::Unretained is safe: the
  // Scheduler is a process-lifetime singleton.
  auto on_done = base::BindOnce(
      [](scoped_refptr<base::SequencedTaskRunner> runner, Scheduler* self,
         std::string job_id, std::string original_target,
         const std::string& status, const std::string& conv_id) {
        if (!runner)
          return;
        runner->PostTask(
            FROM_HERE,
            base::BindOnce(&Scheduler::OnRunComplete, base::Unretained(self),
                           job_id, original_target, status, conv_id));
      },
      runner, base::Unretained(this), job_id, original_target);

  // An app-build job (non-empty cwd) runs the blocking app-build dispatch; a
  // plain chat/browse job runs the chat dispatch. A browse task needs no special
  // route — it is just a chat job whose model is browser-capable, which opens
  // background tabs through the normal POST /tabs path on its own.
  if (!job->cwd.empty()) {
    DispatchScheduledAppBuild(job->message, job->model, job->cwd,
                              job->target_conv_id, std::move(on_done));
  } else {
    DispatchScheduledRun(job->message, job->model, job->target_conv_id,
                         std::move(on_done));
  }
}

void Scheduler::OnRunComplete(const std::string& job_id,
                              const std::string& original_target_conv_id,
                              const std::string& status,
                              const std::string& conv_id) {
  for (Job& j : jobs_) {
    if (j.id != job_id)
      continue;
    j.last_status = status;
    // If the run auto-created a conversation, remember it so future fires append
    // to the same conversation.
    if (original_target_conv_id.empty() && !conv_id.empty())
      j.target_conv_id = conv_id;
    break;
  }
  Save();
}

void Scheduler::ComputeNextFire(Job* job, base::Time now) {
  if (job->interval_sec > 0) {
    job->next_fire_us = ToEpochUs(now + base::Seconds(job->interval_sec));
    return;
  }
  if (job->once_at_us > 0) {
    // One-shot: fire at the requested time. If it is already in the past it
    // will fire on the next poll (catch-up), then self-disable.
    job->next_fire_us = job->once_at_us;
    return;
  }
  // Cron is not evaluated in Phase 2; leave unscheduled.
  job->next_fire_us = 0;
}

base::DictValue Scheduler::ListJobsDict() const {
  base::ListValue list;
  for (const Job& job : jobs_)
    list.Append(JobToDict(job));
  base::DictValue d;
  d.Set("version", 1);
  d.Set("jobs", std::move(list));
  return d;
}

void Scheduler::GetJobsAsync(
    base::OnceCallback<void(base::DictValue)> callback) {
  // jobs_ is only safe to touch on the sequence that owns it (the gateway IO
  // thread, captured as task_runner_ in Start()). Compute the snapshot there and
  // let PostTaskAndReplyWithResult deliver it back on the caller's sequence.
  if (!task_runner_) {
    // Scheduler not started yet: reply with an empty list on the caller's
    // sequence so callers always get a well-formed dict.
    base::DictValue empty;
    empty.Set("version", 1);
    empty.Set("jobs", base::ListValue());
    std::move(callback).Run(std::move(empty));
    return;
  }
  // base::Unretained is safe: the Scheduler is a process-lifetime NoDestructor
  // singleton, so it outlives any UI-thread caller and this posted task.
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce([]() { return Scheduler::Get()->ListJobsDict(); }),
      std::move(callback));
}

base::DictValue Scheduler::UpsertJobFromDict(const base::DictValue& body) {
  Job job = JobFromDict(body);
  if (job.message.empty()) {
    base::DictValue err;
    err.Set("error", "job requires a run message");
    return err;
  }
  if (job.id.empty())
    job.id = "job_" + base::HexEncode(base::RandBytesAsVector(4));

  // Compute the initial next fire from the trigger (relative to now for
  // intervals; the absolute time for one-shots).
  ComputeNextFire(&job, base::Time::Now());

  // Replace an existing job with the same id, otherwise append.
  bool replaced = false;
  for (Job& existing : jobs_) {
    if (existing.id == job.id) {
      // Preserve run history across an edit.
      job.last_fire_us = existing.last_fire_us;
      if (job.last_status.empty())
        job.last_status = existing.last_status;
      existing = job;
      replaced = true;
      break;
    }
  }
  if (!replaced)
    jobs_.push_back(job);
  Save();
  return JobToDict(job);
}

bool Scheduler::RemoveJob(const std::string& id) {
  const size_t before = jobs_.size();
  jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                             [&](const Job& j) { return j.id == id; }),
              jobs_.end());
  const bool removed = jobs_.size() != before;
  if (removed)
    Save();
  return removed;
}

void Scheduler::Load() {
  // Mirror LoadSessions/SaveSessions in grok_native.cc: ReadFileToString +
  // JSONReader, default to empty on any failure.
  base::FilePath path = xplorer_paths::Resolve("schedules.json");
  jobs_.clear();
  if (path.empty())
    return;
  std::string json;
  if (!base::ReadFileToString(path, &json))
    return;
  auto parsed = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!parsed)
    return;
  if (const base::ListValue* list = parsed->FindList("jobs")) {
    for (const auto& v : *list) {
      if (!v.is_dict())
        continue;
      Job job = JobFromDict(v.GetDict());
      if (!job.id.empty())
        jobs_.push_back(std::move(job));
    }
  }
}

void Scheduler::Save() const {
  base::FilePath path = xplorer_paths::Resolve("schedules.json");
  if (path.empty())
    return;
  base::DictValue data = ListJobsDict();
  base::CreateDirectory(path.DirName());
  std::string json;
  if (base::JSONWriter::Write(data, &json))
    base::WriteFile(path, json);
}

// static
base::DictValue Scheduler::JobToDict(const Job& job) {
  base::DictValue d;
  d.Set("id", job.id);
  d.Set("label", job.label);
  d.Set("enabled", job.enabled);

  base::DictValue trigger;
  trigger.Set("cron", job.cron);
  if (job.interval_sec > 0)
    trigger.Set("interval_sec", job.interval_sec);
  else
    trigger.Set("interval_sec", base::Value());
  if (job.once_at_us > 0)
    trigger.Set("once_at_us", base::NumberToString(job.once_at_us));
  else
    trigger.Set("once_at_us", base::Value());
  d.Set("trigger", std::move(trigger));

  // *_us values are stored as strings: they exceed 2^53 and would lose
  // precision as JSON doubles (base::Value has no native 64-bit int).
  d.Set("next_fire_us", base::NumberToString(job.next_fire_us));
  d.Set("last_fire_us", base::NumberToString(job.last_fire_us));
  d.Set("last_status", job.last_status);

  base::DictValue run;
  run.Set("message", job.message);
  run.Set("model", job.model);
  run.Set("target_conv_id", job.target_conv_id);
  run.Set("cwd", job.cwd);
  run.Set("app_id", job.app_id);
  d.Set("run", std::move(run));

  d.Set("max_concurrent_tabs", job.max_concurrent_tabs);
  return d;
}

namespace {

// Read an int64 *_us field that may be stored as a JSON string (the canonical
// form, to preserve precision) or, defensively, as an int/double.
int64_t ReadUsField(const base::DictValue& d, const char* key) {
  if (const std::string* s = d.FindString(key)) {
    int64_t v = 0;
    if (base::StringToInt64(*s, &v))
      return v;
    return 0;
  }
  if (std::optional<double> dbl = d.FindDouble(key))
    return static_cast<int64_t>(*dbl);
  if (std::optional<int> i = d.FindInt(key))
    return *i;
  return 0;
}

}  // namespace

// static
Scheduler::Job Scheduler::JobFromDict(const base::DictValue& d) {
  Job job;
  if (const std::string* id = d.FindString("id"))
    job.id = *id;
  if (const std::string* label = d.FindString("label"))
    job.label = *label;
  job.enabled = d.FindBool("enabled").value_or(true);

  if (const base::DictValue* trigger = d.FindDict("trigger")) {
    if (const std::string* cron = trigger->FindString("cron"))
      job.cron = *cron;
    if (std::optional<int> interval = trigger->FindInt("interval_sec"))
      job.interval_sec = *interval;
    job.once_at_us = ReadUsField(*trigger, "once_at_us");
  }

  job.next_fire_us = ReadUsField(d, "next_fire_us");
  job.last_fire_us = ReadUsField(d, "last_fire_us");
  if (const std::string* status = d.FindString("last_status"))
    job.last_status = *status;

  if (const base::DictValue* run = d.FindDict("run")) {
    if (const std::string* message = run->FindString("message"))
      job.message = *message;
    if (const std::string* model = run->FindString("model"))
      job.model = *model;
    if (const std::string* conv = run->FindString("target_conv_id"))
      job.target_conv_id = *conv;
    if (const std::string* cwd = run->FindString("cwd"))
      job.cwd = *cwd;
    if (const std::string* app_id = run->FindString("app_id"))
      job.app_id = *app_id;
  }

  if (std::optional<int> mct = d.FindInt("max_concurrent_tabs"))
    job.max_concurrent_tabs = *mct;
  return job;
}

}  // namespace agent_gateway
