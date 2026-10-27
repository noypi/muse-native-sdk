"use strict";

// Re-export shim for the Batch-2 validator (packages/compiler/lib/files.js).
// Must not crash `require` before Batch 2 lands `lib/files.js`.
let files = {};
try {
  files = require("./lib/files.js");
} catch {
  files = {};
}

module.exports = { ...files };
