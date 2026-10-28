"use strict";

// T3 — inset vector bus (FR-STATE-001, AC-STATE-008).
//
// Specialized atom channel over `lib/state.js`: a dedicated `state()` atom
// holds the latest `{ top, left, bottom, right }` vector, so an inset-only
// publish invalidates only inset subscribers — product state atoms keep
// their own records and are never touched. Delivery goes through the shared
// scheduler (`lib/scheduler.js`); tests drive `flush()` directly.
//
// Key names top/left/bottom/right stay stable to match the
// `engine/runtime/src/ime_inset.cpp` parse (read-only reference, never
// modified here).

var stateModule = require("./state.js");

var insetAtom = stateModule.state({ top: 0, left: 0, bottom: 0, right: 0 });
var record = stateModule.getRefRecord(insetAtom);

function assertValidInsets(insets) {
  if (insets === null || typeof insets !== "object") {
    throw new TypeError("MUSE_STATE_BAD_INSETS");
  }
  var fields = ["top", "left", "bottom", "right"];
  for (var i = 0; i < fields.length; i += 1) {
    var v = insets[fields[i]];
    if (typeof v !== "number" || !Number.isFinite(v)) {
      throw new TypeError("MUSE_STATE_BAD_INSETS");
    }
  }
}

function publishInsets(insets) {
  // Validate BEFORE any notification: a bad payload throws without
  // touching the atom (hence without scheduling any subscriber job).
  assertValidInsets(insets);
  insetAtom.value = {
    top: insets.top,
    left: insets.left,
    bottom: insets.bottom,
    right: insets.right,
  };
}

function subscribeInsets(cb) {
  if (typeof cb !== "function") {
    throw new TypeError("MUSE_STATE_BAD_INSETS");
  }
  var job = function () {
    cb(insetAtom.value);
  };
  record.subs.add(job);
  var done = false;
  return function () {
    if (done) return;
    done = true;
    record.subs.delete(job);
  };
}

module.exports = { publishInsets: publishInsets, subscribeInsets: subscribeInsets };
