"use strict";

// T5 — lazy cached topological `computed` (FR-MCORE-003).
//
// Lazy on first read, cached until a dependency changes. Evaluation is
// ordered through T2's single shared scheduler so a chain recomputes
// topologically within one flush: each computed owns a stable refresh job,
// and a recompute that changes value (`Object.is`) fans out to downstream
// subscribers by marking them dirty and scheduling their refresh jobs.
// Jobs scheduled during a flush run in the same flush (scheduler FIFO +
// identity dedup), so a chain `state -> a -> b -> c` settles in order with
// exactly one recompute each, and a diamond join runs once after both
// parents have updated (no mixed stale/updated publish, no glitch).
//
// Dependency tracking reuses T4's hook (`lib/state.js`): the getter runs
// under `pushActiveConsumer(consumer)` so state/reactive/readonly reads
// subscribe the computed, and each `.value` read calls
// `trackRef(ownRecord)` so downstream computeds subscribe. Granularity
// note (T4 report): subscriber fan-out is coarse per state holder /
// reactive root (one `notifyJob` per root). That meets the topological
// need — computeds subscribe directly to the roots they read and to the
// upstream computed records they read, so one root-level `schedule` per
// mutation is enough; ordering and exactly-once fall out of the chained
// refresh jobs, not of finer fan-out. No state.js change was needed.
//
// CommonJS, dependency-free pure JS. Usable outside a component.

var scheduler = require("./scheduler.js");
var stateHooks = require("./state.js");

function readonlyWriteError() {
  var err = new Error(
    "MUSE_CORE_READONLY_WRITE: cannot write through a computed view"
  );
  err.name = "MUSE_CORE_READONLY_WRITE";
  err.code = "MUSE_CORE_READONLY_WRITE";
  return err;
}

function computed(getter) {
  if (typeof getter !== "function") {
    throw new TypeError("computed(getter): getter must be a function");
  }

  var value;
  var hasValue = false;
  var dirty = true;
  var computing = false;

  // Downstream record in the T4 shape (`{ subs, notifyJob }`): downstream
  // computeds subscribe via `trackRef`, and a changed recompute fans out
  // by invoking each subscriber's scheduler entry point.
  var ownRecord = { subs: new Set(), notifyJob: null };
  ownRecord.notifyJob = function () {
    var subs = Array.from(ownRecord.subs);
    for (var i = 0; i < subs.length; i += 1) {
      var sub = subs[i];
      try {
        if (typeof sub === "function") sub();
        else if (sub && typeof sub.job === "function") sub.job();
        else if (sub && typeof sub.run === "function") sub.run();
      } catch (e) {
        void e;
      }
    }
  };

  // Consumer registered on upstream dependency records. `deps` holds the
  // upstream `subs` sets so recompute can unsubscribe before re-collecting.
  var consumer = { deps: new Set(), job: null };

  function unsubscribe() {
    consumer.deps.forEach(function (subs) {
      try {
        subs.delete(consumer);
      } catch (e) {
        void e;
      }
    });
    consumer.deps.clear();
  }

  function recompute() {
    if (computing) return value;
    unsubscribe();
    computing = true;
    stateHooks.pushActiveConsumer(consumer);
    var next;
    try {
      next = getter();
    } finally {
      stateHooks.popActiveConsumer();
      computing = false;
    }
    dirty = false;
    if (!hasValue || !Object.is(next, value)) {
      value = next;
      hasValue = true;
      ownRecord.notifyJob();
    }
    return value;
  }

  function refreshJob() {
    if (!dirty) return;
    recompute();
  }

  consumer.job = function () {
    dirty = true;
    scheduler.schedule(refreshJob);
  };

  var handle = {};
  Object.defineProperty(handle, "value", {
    configurable: true,
    enumerable: true,
    get: function () {
      stateHooks.trackRef(ownRecord);
      if (dirty) recompute();
      return value;
    },
    set: function () {
      throw readonlyWriteError();
    },
  });
  return handle;
}

module.exports = { computed: computed };
