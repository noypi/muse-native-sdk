"use strict";

// Re-export shim for the Batch-2 validator (packages/compiler/lib/files.js).
// Must not crash `require` before Batch 2 lands `lib/files.js`.
let files = {};
try {
  files = require("./lib/files.js");
} catch {
  files = {};
}

let builtins = {};
try {
  builtins = require("./lib/builtins.js");
} catch {
  builtins = {};
}

let lists = {};
try {
  lists = require("./lib/lists.js");
} catch {
  lists = {};
}

let style = {};
try {
  style = require("./lib/style.js");
} catch {
  style = {};
}

module.exports = { ...files, ...builtins, ...lists, ...style };
