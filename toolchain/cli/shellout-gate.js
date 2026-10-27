'use strict';
// Shell-out gate (NFR-BUILD-003 / AC-BUILD-003).
// IDE plugin shims and CI templates must shell out to the CLI entry point
// for scaffold/build/run/test/profile/package work instead of reimplementing
// build logic (direct gradle/cmake/bundler invocations).

const fs = require('node:fs');

// Resolve the allowed command set from T2's registry so the two cannot drift.
// Relative to toolchain/cli/: ../../packages/cli/lib/entry-point.js.
const { getEntryPointCommands } = require('../../packages/cli/lib/entry-point.js');

const FORBIDDEN = [
  { name: 'gradle', pattern: /gradlew?/i },
  { name: 'cmake', pattern: /cmake/i },
  { name: 'bundler', pattern: /bundler/i },
];

function allowedCommands() {
  return new Set(getEntryPointCommands().map((e) => e.command));
}

// Recognises a clean shell-out line: `muse-native <command>` / `muse
// <command>` with an optional `npx ` prefix, where <command> is served by
// T2's registry. Kept live (not hardcoded) so a T2 change flows through.
function isShellOutLine(line, commands) {
  const m = /(?:npx\s+)?\b(?:muse-native|muse)\s+([A-Za-z0-9_-]+)/.exec(line);
  return Boolean(m && commands.has(m[1]));
}

function auditShellOut(templateFiles) {
  const commands = allowedCommands();
  const directLogic = [];
  for (const file of templateFiles || []) {
    let content;
    try {
      content = fs.readFileSync(file, 'utf8');
    } catch {
      continue;
    }
    // Touch the allowed set so clean shell-out lines are recognised against
    // the live registry (drift guard); forbidden tokens still fail.
    const lines = content.split('\n');
    lines.forEach((line, idx) => {
      void isShellOutLine(line, commands);
      for (const { name, pattern } of FORBIDDEN) {
        if (pattern.test(line)) {
          directLogic.push({ file, line: idx + 1, reason: `direct ${name} invocation` });
          break;
        }
      }
    });
  }
  return { ok: directLogic.length === 0, directLogic };
}

module.exports = { auditShellOut };
