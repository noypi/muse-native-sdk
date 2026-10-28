'use strict';
// TC-SAMPLE-002 spec-path entry point (AC-SAMPLE-002, FR-SAMPLE-001, batch 1 T2).
//
// Shim: the single logic source is `./story-coverage.test.ts` (script
// `test:stories` points there). This file exists only to reconcile the spec TC
// path `examples/m3-gallery/test/stories.test.ts` with that file, so
// `node --test test/stories.test.ts` exercises the identical contract.
// No assertions live here; add coverage to `story-coverage.test.ts`.
//
// Hermetic: inherits the hermeticity of the required file (node:test +
// node:assert/strict only, in-memory fixtures, no network/secrets/services).

require('./story-coverage.test.ts');
