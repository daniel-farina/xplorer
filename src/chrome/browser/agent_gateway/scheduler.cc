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

// --- Standard 5-field cron evaluation (minute hour day-of-month month
// day-of-week), self-contained, no new deps. Used by ComputeNextFire when a job
// carries a cron string instead of an interval/once trigger. ---
//
// A single cron field is a comma list of terms; each term is one of:
//   *            any value in [lo, hi]
//   N            a single number
//   */S          every S-th value across the whole [lo, hi] range
//   A-B          inclusive range
//   A-B/S        inclusive range, stepped by S
// CronFieldMatches returns true if |value| satisfies the field; on any malformed
// term it returns false (the caller treats a non-matching/malformed expression
// as "never").

// Matches one comma-separated term against |value| within [lo, hi]. Returns
// false on a malformed term so a bad expression never matches (=> "never").
bool CronTermMatches(const std::string& term, int value, int lo, int hi) {
  if (term.empty())
    return false;

  // Split an optional "/step" suffix.
  std::string base_part = term;
  int step = 1;
  if (size_t slash = term.find('/'); slash != std::string::npos) {
    base_part = term.substr(0, slash);
    const std::string step_str = term.substr(slash + 1);
    if (step_str.empty() || !base::StringToInt(step_str, &step) || step <= 0)
      return false;
  }

  int range_lo = lo;
  int range_hi = hi;
  if (base_part == "*") {
    // Whole range (optionally stepped via the "/step" handled above).
  } else if (size_t dash = base_part.find('-');
             dash != std::string::npos) {
    // A-B inclusive range.
    const std::string lo_str = base_part.substr(0, dash);
    const std::string hi_str = base_part.substr(dash + 1);
    if (!base::StringToInt(lo_str, &range_lo) ||
        !base::StringToInt(hi_str, &range_hi))
      return false;
  } else {
    // Single number. With a "/step" suffix (e.g. "5/10") cron treats it as a
    // range from the number to the field maximum; without a step it is exact.
    int single = 0;
    if (!base::StringToInt(base_part, &single))
      return false;
    range_lo = single;
    range_hi = (step > 1) ? hi : single;
  }

  // Validate the resolved range against the field bounds.
  if (range_lo < lo || range_hi > hi || range_lo > range_hi)
    return false;
  if (value < range_lo || value > range_hi)
    return false;
  // Honor the step relative to the range start.
  return ((value - range_lo) % step) == 0;
}

// Returns true if |value| matches the cron |field| (a comma list of terms)
// within [lo, hi]. A malformed term causes the whole field to fail to match.
bool CronFieldMatches(const std::string& field, int value, int lo, int hi) {
  if (field.empty())
    return false;
  size_t start = 0;
  while (start <= field.size()) {
    size_t comma = field.find(',', start);
    const std::string term = (comma == std::string::npos)
                                 ? field.substr(start)
                                 : field.substr(start, comma - start);
    if (CronTermMatches(term, value, lo, hi))
      return true;
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  return false;
}

// Returns true if the 5-field cron |expr| matches the exploded local time |e|.
// |e.day_of_week| is 0=Sunday..6=Saturday, which is exactly cron's dow
// convention. Standard semantics for the day fields: minute AND hour AND month
// must all match, combined with the day match. The day match is:
//   - if BOTH day-of-month and day-of-week are restricted (neither is "*"),
//     the day matches when EITHER one matches (the cron "OR when both
//     restricted" rule);
//   - otherwise (one or both are "*"), the day fields are ANDed normally.
// On a malformed expression (wrong field count, etc.) returns false so the
// caller leaves the job unscheduled ("never").
bool CronMatches(const std::string& expr, const base::Time::Exploded& e) {
  // Split on whitespace into exactly five fields.
  std::vector<std::string> fields;
  size_t i = 0;
  while (i < expr.size()) {
    while (i < expr.size() && (expr[i] == ' ' || expr[i] == '\t'))
      ++i;
    size_t begin = i;
    while (i < expr.size() && expr[i] != ' ' && expr[i] != '\t')
      ++i;
    if (i > begin)
      fields.push_back(expr.substr(begin, i - begin));
  }
  if (fields.size() != 5)
    return false;

  const std::string& f_min = fields[0];
  const std::string& f_hour = fields[1];
  const std::string& f_dom = fields[2];
  const std::string& f_mon = fields[3];
  const std::string& f_dow = fields[4];

  if (!CronFieldMatches(f_min, e.minute, 0, 59))
    return false;
  if (!CronFieldMatches(f_hour, e.hour, 0, 23))
    return false;
  if (!CronFieldMatches(f_mon, e.month, 1, 12))
    return false;

  const bool dom_restricted = (f_dom != "*");
  const bool dow_restricted = (f_dow != "*");
  const bool dom_ok = CronFieldMatches(f_dom, e.day_of_month, 1, 31);
  const bool dow_ok = CronFieldMatches(f_dow, e.day_of_week, 0, 6);

  bool day_ok;
  if (dom_restricted && dow_restricted) {
    // Both restricted: OR them (standard Vixie-cron behavior).
    day_ok = dom_ok || dow_ok;
  } else {
    // One (or both) is "*": AND. A "*" field always matches its own [lo, hi],
    // so this reduces to "the restricted field, if any, must match".
    day_ok = dom_ok && dow_ok;
  }
  return day_ok;
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
    if (job.last_status == "running")
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
  } else if (!job->cron.empty()) {
    ComputeNextFire(job, now);
  }

  DispatchJob(*job);
}

void Scheduler::DispatchJob(const Job& job) {
  const std::string job_id = job.id;
  const std::string original_target = job.target_conv_id;
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
  if (!job.cwd.empty()) {
    DispatchScheduledAppBuild(job.message, job.model, job.cwd,
                              job.target_conv_id, std::move(on_done));
  } else {
    // Pass the job's soft tab cap; DispatchScheduledRun appends a one-line
    // "open at most N tabs" instruction to the message when it is > 0. This is
    // an LLM-respected hint only, not a hard limit, because the grok agent does
    // not attribute the tabs it opens to a particular task.
    DispatchScheduledRun(job.message, job.model, job.target_conv_id,
                         job.max_concurrent_tabs, std::move(on_done));
  }
}

base::DictValue Scheduler::RunJobNow(const std::string& id) {
  Job* job = nullptr;
  for (Job& j : jobs_) {
    if (j.id == id) {
      job = &j;
      break;
    }
  }
  if (!job) {
    base::DictValue err;
    err.Set("error", "job not found");
    return err;
  }

  // Same 409 guard the message handler uses: if a run for this job's target
  // conversation is already active, refuse rather than racing the session store.
  // (A job with an empty target_conv_id auto-creates a conversation on dispatch,
  // so there is nothing to collide with yet — only guard a known conversation.)
  if (!job->target_conv_id.empty() &&
      IsConversationRunActive(job->target_conv_id)) {
    base::DictValue err;
    err.Set("error", "job is already running");
    return err;
  }

  // Stamp the fire like FireJob does, but do NOT recompute next_fire_us: a manual
  // run is out-of-band and must not perturb the regular schedule.
  job->last_fire_us = ToEpochUs(base::Time::Now());
  job->last_status = "running";
  DispatchJob(*job);
  Save();

  base::DictValue ok;
  ok.Set("ok", true);
  return ok;
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

    // Record this run at the head of the history (newest first), capping to the
    // most recent kMaxHistory records. |last_fire_us| was stamped when the run
    // fired (FireJob / RunJobNow); fall back to now if it is somehow unset.
    RunRecord record;
    record.fired_us =
        j.last_fire_us > 0 ? j.last_fire_us : ToEpochUs(base::Time::Now());
    record.status = status;
    record.conv_id = conv_id;
    j.history.insert(j.history.begin(), std::move(record));
    if (j.history.size() > kMaxHistory)
      j.history.resize(kMaxHistory);
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
  if (!job->cron.empty()) {
    // Cron trigger: find the next LOCAL minute strictly after |now| whose
    // exploded time matches the expression. We iterate minute-by-minute (cron's
    // resolution is one minute) starting at |now| rounded UP to the next whole
    // minute, so a candidate is always strictly in the future. The search is
    // bounded to ~1 year; if nothing matches in that window (e.g. an
    // unsatisfiable or malformed expression — CronMatches returns false for the
    // latter) we leave the job unscheduled ("never").
    base::Time::Exploded e;
    now.LocalExplode(&e);
    // Round up to the next whole minute: zero seconds/millis and advance one
    // minute so we never re-fire the current (already-past) minute.
    e.second = 0;
    e.millisecond = 0;
    base::Time candidate;
    if (!base::Time::FromLocalExploded(e, &candidate)) {
      // The truncated-to-minute local time was invalid (e.g. a DST spring-
      // forward gap). Fall back to |now| itself as the search anchor; the
      // minute-stepping below will walk past the gap.
      candidate = now;
    }
    candidate += base::Minutes(1);

    constexpr int kMaxMinutes = 366 * 24 * 60;  // ~1 year search cap.
    job->next_fire_us = 0;  // Default to "never" unless a match is found.
    for (int step = 0; step < kMaxMinutes; ++step) {
      base::Time::Exploded ce;
      candidate.LocalExplode(&ce);
      if (CronMatches(job->cron, ce)) {
        job->next_fire_us = ToEpochUs(candidate);
        return;
      }
      candidate += base::Minutes(1);
    }
    // No match within the cap: leave next_fire_us = 0 (never).
    return;
  }
  // No usable trigger: leave unscheduled.
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
      // Preserve run state + history across an edit (the edit body carries the
      // trigger/run fields, not the canonical run log).
      job.last_fire_us = existing.last_fire_us;
      if (job.last_status.empty())
        job.last_status = existing.last_status;
      job.history = std::move(existing.history);
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

  // Run history, NEWEST FIRST (job.history is already maintained that way). Each
  // record's fired_us is emitted as a STRING, matching the *_us-as-string
  // convention above (precision-safe). The /schedules detail view reads
  // job.history = [{fired_us, status, conv_id}].
  base::ListValue history;
  for (const RunRecord& r : job.history) {
    base::DictValue rec;
    rec.Set("fired_us", base::NumberToString(r.fired_us));
    rec.Set("status", r.status);
    rec.Set("conv_id", r.conv_id);
    history.Append(std::move(rec));
  }
  d.Set("history", std::move(history));
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

  // Run history, stored NEWEST FIRST with fired_us as a string. Preserve order
  // and cap defensively to kMaxHistory in case the file holds more.
  if (const base::ListValue* history = d.FindList("history")) {
    for (const auto& v : *history) {
      if (!v.is_dict())
        continue;
      const base::DictValue& rec = v.GetDict();
      RunRecord r;
      r.fired_us = ReadUsField(rec, "fired_us");
      if (const std::string* status = rec.FindString("status"))
        r.status = *status;
      if (const std::string* conv = rec.FindString("conv_id"))
        r.conv_id = *conv;
      job.history.push_back(std::move(r));
      if (job.history.size() >= kMaxHistory)
        break;
    }
  }
  return job;
}

}  // namespace agent_gateway
