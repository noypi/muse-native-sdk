"use strict";

// T2 — single shared scheduler/flush module (FR-MCORE-002..004 batching half).
//
// One queue shared by every state/effect/computed notification: `schedule`
// enqueues, `flush` drains, `batch` defers the drain to the outermost exit.
// Dependency-free pure JS; deterministic — tests drive `flush()` directly,
// with a microtask auto-flush at tick end for production writes.
//
// Dedup is by job identity against the currently-queued set: scheduling the
// same fn repeatedly before a flush runs it once, while a job that
// reschedules itself after running re-queues (bounded by MAX_JOBS, else the
// scheduler-overflow error). Identical-value writes never reach this module:
// that no-op lives at the T4 write path, which simply does not call
// `schedule` when `Object.is` reports no change.

var MAX_JOBS = 100;

var queue = [];
var queued = new Set();
var isFlushing = false;
var batchDepth = 0;
var autoFlushQueued = false;

// T5 — `getInvalidatedPaths` test seam (FR-STATE-001 AC-STATE-007/010).
// Additive bookkeeping only: each distinct job fn gets a stable opaque id
// ("sched-<n>", never "widget"-prefixed); ids of jobs dequeued by `flush`
// accumulate until read, `getInvalidatedPaths` returns them sorted then
// clears. Never throws and never alters schedule/flush/batch semantics.
var jobIds = new WeakMap();
var nextJobId = 1;
var flushedIds = new Set();

function idFor(job) {
  var id = jobIds.get(job);
  if (id === undefined) {
    id = "sched-" + nextJobId;
    nextJobId += 1;
    jobIds.set(job, id);
  }
  return id;
}

// Test seam: sorted ids of jobs flushed since the last read, then clears.
// Returns `string[]`; `[]` when nothing flushed; never throws.
function getInvalidatedPaths() {
  try {
    var out = Array.from(flushedIds);
    out.sort();
    flushedIds.clear();
    return out;
  } catch (e) {
    void e;
    try {
      flushedIds.clear();
    } catch (clearErr) {
      void clearErr;
    }
    return [];
  }
}

function requestAutoFlush() {
  if (autoFlushQueued) return;
  autoFlushQueued = true;
  var run = function () {
    autoFlushQueued = false;
    if (queue.length === 0 || isFlushing || batchDepth > 0) return;
    flush();
  };
  if (typeof queueMicrotask === "function") {
    queueMicrotask(run);
  } else {
    Promise.resolve().then(run);
  }
}

// Enqueue a job for the next flush. Scheduling the same fn while it is
// already queued is a no-op (one-flush coalescing, AC-MCORE-009 analogue).
function schedule(job) {
  if (typeof job !== "function") {
    throw new TypeError("schedule(job): job must be a function");
  }
  if (queued.has(job)) return;
  queued.add(job);
  queue.push(job);
  try {
    idFor(job);
  } catch (e) {
    void e;
  }
  if (batchDepth === 0 && !isFlushing) requestAutoFlush();
}

// Run the queue once: each queued job runs at most once per pass, jobs
// scheduled during the drain join the same flush, and a reschedule loop
// past MAX_JOBS throws the scheduler-overflow error. A throwing job is
// recorded and the drain continues; the first job error is rethrown after
// the queue drains (an overflow error takes precedence).
function flush() {
  if (isFlushing) return;
  if (queue.length === 0) return;
  isFlushing = true;
  var firstError = null;
  var ran = 0;
  try {
    while (queue.length > 0) {
      if (ran >= MAX_JOBS) {
        var overflow = new Error(
          "scheduler overflow: job queue exceeded " + MAX_JOBS + " jobs in one flush"
        );
        overflow.name = "SchedulerOverflowError";
        overflow.code = "MUSE_CORE_SCHEDULER_OVERFLOW";
        // Abort the poisoned drain: the looping job is still queued, so
        // drop the remainder rather than leaving it for the next flush
        // (or the tick-end auto-flush) to trip over again.
        queue.length = 0;
        queued.clear();
        throw overflow;
      }
      var job = queue.shift();
      queued.delete(job);
      ran += 1;
      try {
        flushedIds.add(idFor(job));
      } catch (e) {
        void e;
      }
      try {
        job();
      } catch (err) {
        if (firstError === null) firstError = err;
      }
    }
  } finally {
    // A job that throws mid-drain must not strand later jobs behind a
    // stuck flushing flag; remaining jobs stay queued for the next flush.
    isFlushing = false;
  }
  if (firstError !== null && firstError !== undefined) throw firstError;
}

// Run `fn` with the auto-flush deferred until `fn` returns; nested `batch`
// collapses to the outermost exit. `fn` exceptions propagate to the caller
// and deferred jobs still flush before the error reaches it.
function batch(fn) {
  if (typeof fn !== "function") {
    throw new TypeError("batch(fn): fn must be a function");
  }
  batchDepth += 1;
  var fnError = null;
  var hasFnError = false;
  try {
    fn();
  } catch (err) {
    fnError = err;
    hasFnError = true;
  }
  batchDepth -= 1;
  if (batchDepth === 0) {
    if (hasFnError) {
      try {
        flush();
      } catch (flushErr) {
        if (
          fnError !== null &&
          (typeof fnError === "object" || typeof fnError === "function") &&
          fnError.cause === undefined
        ) {
          try {
            fnError.cause = flushErr;
          } catch (e) {
            void e;
          }
        }
      }
      throw fnError;
    }
    flush();
  } else if (hasFnError) {
    throw fnError;
  }
}

module.exports = { schedule, flush, batch, getInvalidatedPaths };
