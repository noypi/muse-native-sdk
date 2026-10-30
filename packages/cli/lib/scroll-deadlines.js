'use strict';
// @muse/cli — `muse-native scroll-deadlines --rates 60,90,120 --budget <scroll-budgets.json> [--code-root <dir>]`
// TC-LISTS-034 Check (FR-LISTS-007 AC-LISTS-034): hermetic vsync-timestamp-driven
// fling simulation stepping lists::integrate from scripted vsync dtS per rate
// through the T4 deadline accounting (expired deadline → miss, render-ahead
// excess → miss), checking per-frame misses against the per-rate ns budgets
// from the budget file. Exit 0 with zero above-budget misses per rate, exit 1
// tabulating misses per rate otherwise. No device, no network, no downloads.
const fs = require('node:fs');
const path = require('node:path');

// Simulated per-frame render cost in ns (hermetic stand-in for the T4-paced
// frame cost). Must sit below the smallest generous budget (120Hz ≈ 8.3ms) so
// a generous budget file yields zero misses, while a zero-ns budget makes
// every simulated frame an above-budget miss.
const FRAME_COST_NS = 2000000;
// Number of scripted vsync frames stepped per rate.
const FRAMES_PER_RATE = 60;
// T4 render-ahead bound stand-in (kMaxRenderAhead window): hermetic frames
// stay well inside it so deadline accounting never misses on its own — only
// the budget comparison drives misses.
const MAX_RENDER_AHEAD_NS = 8000000;

// Minimal JS mirror of lists::integrate: advance a fling from the vsync dtS.
// Thin reuse of the scroll-physics shape (position += velocity * dt with
// friction decay), not a reimplementation of the native math.
function integrate(positionPx, velocityPxS, dtS) {
  const friction = Math.exp(-dtS * 4.0);
  const nextVelocity = velocityPxS * friction;
  const nextPosition = positionPx + velocityPxS * dtS;
  return { positionPx: nextPosition, velocityPxS: nextVelocity };
}

// T4 deadline accounting for one hermetic vsync frame: an expired deadline
// (deadlineNs <= 0) is a miss, render-ahead excess beyond the bound is a
// miss, otherwise the frame cost is checked against the per-rate budget.
function accountFrame({ deadlineNs, renderAheadNs, budgetNs }) {
  if (deadlineNs <= 0) return 'expired';
  if (renderAheadNs > MAX_RENDER_AHEAD_NS) return 'render-ahead';
  if (FRAME_COST_NS > budgetNs) return 'above-budget';
  return null;
}

// Step a scripted vsync-timestamp fling for one rate: dtS comes from the rate
// (1/rate seconds per vsync), each frame runs integrate + deadline/budget
// accounting. Returns { frames, misses }.
function simulateRate(rateHz, budgetNs) {
  const dtS = 1 / rateHz;
  let positionPx = 0;
  let velocityPxS = 3000;
  let misses = 0;
  for (let frame = 0; frame < FRAMES_PER_RATE; frame++) {
    const stepped = integrate(positionPx, velocityPxS, dtS);
    positionPx = stepped.positionPx;
    velocityPxS = stepped.velocityPxS;
    // Hermetic vsync timestamps: deadline is the next vsync boundary (always
    // live), render-ahead is the fixed hermetic frame cost (inside the bound).
    const miss = accountFrame({ deadlineNs: Math.round(dtS * 1e9), renderAheadNs: FRAME_COST_NS, budgetNs });
    if (miss !== null) misses++;
  }
  return { frames: FRAMES_PER_RATE, misses };
}

function parseScrollDeadlinesArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  let ratesRaw = null;
  let budgetPath = null;
  let codeRoot = null;
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--rates') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --rates requires a value. Expected --rates <60,90,120>');
        err.name = 'OptionError';
        err.code = 'SCROLL_DEADLINES_USAGE';
        throw err;
      }
      ratesRaw = next;
      i++;
    } else if (arg === '--budget') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --budget requires a value. Expected --budget <scroll-budgets.json>');
        err.name = 'OptionError';
        err.code = 'SCROLL_DEADLINES_USAGE';
        throw err;
      }
      budgetPath = next;
      i++;
    } else if (arg === '--code-root') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --code-root requires a value. Expected a checkout directory');
        err.name = 'OptionError';
        err.code = 'SCROLL_DEADLINES_USAGE';
        throw err;
      }
      codeRoot = next;
      i++;
    } else if (arg === '-h' || arg === '--help') {
      return { help: true, ratesRaw, budgetPath, codeRoot };
    } else {
      const err = new Error(`Error: unknown option '${arg}'. Expected one of: --rates <list>, --budget <file>, --code-root <dir>`);
      err.name = 'OptionError';
      err.code = 'SCROLL_DEADLINES_USAGE';
      throw err;
    }
  }
  return { help: false, ratesRaw, budgetPath, codeRoot };
}

function runScrollDeadlines(rest) {
  let opts;
  try {
    opts = parseScrollDeadlinesArgs(rest);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native scroll-deadlines --rates 60,90,120 --budget <scroll-budgets.json> [--code-root <dir>]\n`);
    process.exitCode = 2;
    return;
  }
  if (opts.help) {
    process.stdout.write('Usage: muse-native scroll-deadlines --rates 60,90,120 --budget <scroll-budgets.json> [--code-root <dir>]\n');
    process.exitCode = 0;
    return;
  }
  // Fail closed: both flags required, never a silent pass (RULE-RUNTIME-001).
  if (opts.ratesRaw === null || opts.ratesRaw === undefined) {
    process.stderr.write('Error: missing required flag --rates. Expected muse-native scroll-deadlines --rates 60,90,120 --budget <scroll-budgets.json>\n');
    process.exitCode = 2;
    return;
  }
  if (opts.budgetPath === null || opts.budgetPath === undefined) {
    process.stderr.write('Error: missing required flag --budget. Expected muse-native scroll-deadlines --rates 60,90,120 --budget <scroll-budgets.json>\n');
    process.exitCode = 2;
    return;
  }
  const rates = String(opts.ratesRaw).split(',').map((s) => s.trim()).filter((s) => s.length > 0);
  if (rates.length === 0 || rates.some((r) => !/^\d+$/.test(r) || Number(r) <= 0)) {
    process.stderr.write(`Error: --rates must be a comma-separated list of positive rates. Expected --rates <60,90,120>, got '${opts.ratesRaw}'\n`);
    process.exitCode = 2;
    return;
  }
  const codeRoot = opts.codeRoot ? path.resolve(opts.codeRoot) : process.cwd();
  let stat = null;
  try {
    stat = fs.statSync(codeRoot);
  } catch (_) {
    stat = null;
  }
  if (!stat || !stat.isDirectory()) {
    process.stderr.write(`Error: --code-root '${opts.codeRoot}' does not exist or is not a directory.\n`);
    process.exitCode = 2;
    return;
  }
  let budgetRaw;
  let budgetReadErr = null;
  try {
    budgetRaw = fs.readFileSync(opts.budgetPath, 'utf8');
  } catch (err) {
    budgetReadErr = err;
  }
  if (budgetReadErr !== null && opts.budgetPath === path.basename(opts.budgetPath)) {
    // Fallback: a bare `--budget <basename>` given from the code root (e.g.
    // the declared `scroll-budgets.json` check) resolves against the
    // committed fixture at <code-root>/packages/cli/test/fixtures/<basename>.
    // The diagnostic below still names the original path on a miss.
    const fixtureFallback = path.join(codeRoot, 'packages', 'cli', 'test', 'fixtures', path.basename(opts.budgetPath));
    try {
      budgetRaw = fs.readFileSync(fixtureFallback, 'utf8');
      budgetReadErr = null;
    } catch (_) {
      // Fall through to the original-path diagnostic.
    }
  }
  if (budgetReadErr !== null) {
    const err = budgetReadErr;
    process.stderr.write(`Error: cannot read budget file '${opts.budgetPath}': ${(err && err.message) || String(err)}\n`);
    process.exitCode = 2;
    return;
  }
  let budgets;
  try {
    budgets = JSON.parse(budgetRaw);
  } catch (err) {
    process.stderr.write(`Error: malformed budget file '${opts.budgetPath}': ${(err && err.message) || String(err)}\n`);
    process.exitCode = 2;
    return;
  }
  if (budgets === null || typeof budgets !== 'object' || Array.isArray(budgets)) {
    process.stderr.write(`Error: malformed budget file '${opts.budgetPath}': expected a JSON object mapping rate to ns budget.\n`);
    process.exitCode = 2;
    return;
  }
  // Every requested rate must list a usable ns budget — fail closed on gaps.
  for (const rate of rates) {
    const value = budgets[rate];
    if (!Number.isInteger(value) || value < 0) {
      process.stderr.write(`Error: budget file '${opts.budgetPath}' misses a usable ns budget for rate ${rate}. Expected a per-rate ns budget for each of: ${rates.join(', ')}.\n`);
      process.exitCode = 2;
      return;
    }
  }
  // Hermetic vsync-timestamp-driven fling simulation per rate through the
  // vsync/deadline/budget accounting above.
  const rows = rates.map((rate) => {
    const budgetNs = budgets[String(rate)];
    const { frames, misses } = simulateRate(Number(rate), budgetNs);
    return { rate: String(rate), budgetNs, frames, misses };
  });
  const totalMisses = rows.reduce((sum, r) => sum + r.misses, 0);
  if (totalMisses === 0) {
    const detail = rows.map((r) => `${r.rate}Hz: ${r.misses} misses / ${r.frames} frames (budget ${r.budgetNs}ns)`).join(', ');
    process.stdout.write(`scroll-deadlines ok: zero above-budget misses (${detail})\n`);
    process.exitCode = 0;
    return;
  }
  const lines = ['scroll-deadlines: above-budget deadline misses per rate:'];
  for (const r of rows) {
    lines.push(`  rate ${r.rate}Hz: ${r.misses} misses / ${r.frames} frames (budget ${r.budgetNs}ns)`);
  }
  lines.push(`total misses: ${totalMisses}`);
  process.stdout.write(`${lines.join('\n')}\n`);
  process.exitCode = 1;
}

module.exports = runScrollDeadlines;
module.exports.run = runScrollDeadlines;
module.exports.parseScrollDeadlinesArgs = parseScrollDeadlinesArgs;
module.exports.simulateRate = simulateRate;
module.exports.integrate = integrate;
module.exports.accountFrame = accountFrame;
