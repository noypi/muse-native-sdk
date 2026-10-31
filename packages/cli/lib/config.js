'use strict';
// @muse/cli — typed config load with precedence + printable resolution.
// Schema owns `name: string` / `version: string`. Layers, highest first:
// CLI flag (`--<key> <value>`), env (`MUSE_NATIVE_<KEY>` upper-snake),
// file (`muse-native.yaml`, then deprecated read-only alias fallback
// `muse.config.ts` / `capo.config.ts`, same flat `key: value` line format),
// then built-in defaults. Every resolved key names its winning layer.
// Unknown keys and wrong value types throw ConfigError and never return
// a partial config.
const fs = require('node:fs');
const path = require('node:path');

const ALL_ARCHS = ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'];
const PACKAGING_VALUES = ['fat', 'split', 'both'];
const RENDERER_VALUES = ['cpu', 'egl', 'skia'];

const SCHEMA = {
  name: 'string',
  version: 'string',
  withSample: 'boolean',
  abi: 'string',
  release: 'boolean',
  modules: 'string',
  output: 'string',
  strict: 'boolean',
  diagnostics: 'object',
  archs: 'object',
  packaging: 'string',
  renderer: 'string',
  keystore: 'string',
  keyAlias: 'string',
  storepass: 'string',
};

const DEFAULTS = {
  name: 'hello-app',
  version: '0.0.1',
  withSample: false,
  abi: undefined,
  release: false,
  modules: undefined,
  output: undefined,
  strict: false,
  diagnostics: undefined,
  archs: ALL_ARCHS.slice(),
  packaging: 'fat',
  renderer: 'cpu',
  keystore: path.join(__dirname, '..', 'templates', 'android', 'debug.keystore'),
  keyAlias: 'androiddebugkey',
  storepass: undefined,
};

const ENV_PREFIX = 'MUSE_NATIVE_';

const CANONICAL_FILE = 'muse-native.yaml';
const ALIAS_FILES = ['muse.config.ts', 'capo.config.ts'];

// Error thrown for unknown keys / wrong value types. Carries the key,
// the file the bad value came from, and the expected type name.
// Optional 4th `code` (e.g. CFG-UNKNOWN-ABI, CFG-BAD-PACKAGING) is stored
// on `err.code` and prefixed to the message; value + accepted set stay in
// the message so fail-closed errors name what was rejected.
class ConfigError extends Error {
  constructor(key, file, expected, code) {
    super(code ? `${code}: ${key} in ${file}: expected ${expected}` : `Error: ${key} in ${file}: expected ${expected}`);
    this.name = 'ConfigError';
    this.key = key;
    this.file = file;
    this.expected = expected;
    if (code) this.code = code;
  }
}

// Parse an `archs` layer value into an array, preserving author order.
// Accepts comma-separated scalars (`archs: arm64-v8a, x86_64`) and YAML
// flow-lists (`archs: [arm64-v8a, x86_64]`); single values become a
// one-element array. Non-string/non-array input passes through so the
// validator below can reject it fail-closed.
function parseArchsValue(value) {
  if (Array.isArray(value)) return value.map((v) => String(v).trim());
  if (typeof value !== 'string') return value;
  let raw = value.trim();
  if (raw === '') return [];
  if (raw.startsWith('[') && raw.endsWith(']')) raw = raw.slice(1, -1).trim();
  if (raw === '') return [];
  return raw.split(',').map((v) => v.trim());
}

// Fail-closed enum checks, run at `configure` before compilation. Empty or
// unknown selections never default silently — they throw.
function assertArchsList(list, where) {
  if (!Array.isArray(list)) {
    throw new ConfigError('archs', where, `one of: ${ALL_ARCHS.join(', ')} (got ${JSON.stringify(list)})`, 'CFG-UNKNOWN-ABI');
  }
  if (list.length === 0 || list.some((v) => v === '')) {
    throw new ConfigError('archs', where, `non-empty array, expected one of: ${ALL_ARCHS.join(', ')} (got empty selection)`, 'CFG-UNKNOWN-ABI');
  }
  for (const abi of list) {
    if (!ALL_ARCHS.includes(abi)) {
      throw new ConfigError('archs', where, `unknown ABI "${abi}", expected one of: ${ALL_ARCHS.join(', ')}`, 'CFG-UNKNOWN-ABI');
    }
  }
}

function assertPackagingValue(value, where) {
  if (!PACKAGING_VALUES.includes(value)) {
    throw new ConfigError('packaging', where, `unknown packaging "${value}", expected one of: ${PACKAGING_VALUES.join(', ')}`, 'CFG-BAD-PACKAGING');
  }
}

function assertRendererValue(value, where) {
  if (!RENDERER_VALUES.includes(value)) {
    throw new ConfigError('renderer', where, `unknown renderer "${value}", expected one of: ${RENDERER_VALUES.join(', ')}`, 'CFG-BAD-RENDERER');
  }
}

// Schema validator: fail closed on unknown keys / wrong value types.
// Throws ConfigError naming the key, the file, and the expected type;
// never returns a partial config. Called by resolveConfig before any
// compile/stage work, and before anything is merged or returned.
function validateConfig(raw, file) {
  const where = typeof file === 'string' && file ? file : '<config>';
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)) {
    throw new ConfigError('config', where, 'object');
  }
  for (const key of Object.keys(raw)) {
    // FR-PACKAGING-009 T5 file-only pin: `sdk:` is accepted in
    // muse-native.yaml but never enters the scalar map (muse: precedent).
    if (key === 'sdk') continue;
    if (!Object.prototype.hasOwnProperty.call(SCHEMA, key)) {
      throw new ConfigError(key, where, 'unknown key, expected one of: ' + Object.keys(SCHEMA).join(', '));
    }
  }
  for (const key of Object.keys(raw)) {
    // File-only `sdk:` pin: must be a string when present; never merged.
    if (key === 'sdk') {
      if (typeof raw[key] !== 'string') {
        throw new ConfigError(key, where, 'string');
      }
      continue;
    }
    const expected = SCHEMA[key];
    if (key === 'archs') {
      if (typeof raw[key] === 'string') raw[key] = parseArchsValue(raw[key]);
      if (!Array.isArray(raw[key])) {
        throw new ConfigError(key, where, expected);
      }
      continue;
    }
    if (typeof raw[key] !== expected) {
      throw new ConfigError(key, where, expected);
    }
  }
  // T4 (AC-WORKFLOW-013): every `diagnostics` map entry must name an
  // accepted severity. A bad value throws ConfigError naming the code and
  // `error|warn|off`, at configure time before anything merges. Plain
  // objects only — other shapes are already rejected by the typeof check
  // above (or never arise from the scalar file parse).
  const diag = raw.diagnostics;
  if (diag !== undefined && diag !== null && typeof diag === 'object' && !Array.isArray(diag)) {
    for (const code of Object.keys(diag)) {
      if (code === '__proto__' || code === 'constructor' || code === 'prototype') {
        throw new ConfigError(code, where, 'rejected dangerous key');
      }
      if (diag[code] !== 'error' && diag[code] !== 'warn' && diag[code] !== 'off') {
        throw new ConfigError(code, where, 'error|warn|off');
      }
    }
  }
  // T1 (AC-PACKAGING-005, RULE-WORKFLOW-006): archs/packaging enums are
  // validated at configure time, before anything merges. Unknown ABIs name
  // the value + accepted set (CFG-UNKNOWN-ABI); bad packaging names the
  // value + fat/split/both (CFG-BAD-PACKAGING); empty never defaults.
  if (raw.archs !== undefined) assertArchsList(raw.archs, where);
  if (raw.packaging !== undefined) assertPackagingValue(raw.packaging, where);
  if (raw.renderer !== undefined) assertRendererValue(raw.renderer, where);
}

// T4 (AC-WORKFLOW-016, FR-WORKFLOW-006): report requested vs effective
// renderer separately; never overwrite the preference. `requested` echoes
// `resolvedConfig.config.renderer` verbatim. `effective` equals
// `probe.effective` when a valid renderer value is supplied, else
// `requested`. `downgraded` is true iff `effective !== requested`. The
// downgrade itself stays engine-owned (FR-GRAPHICS-014) — this helper only
// records the probe outcome. Never mutates `resolvedConfig.config.renderer`.
// A `trigger` string is preserved only when the probe outcome was accepted;
// ignored probes (invalid/absent `effective`) fall back with no trigger.
function resolveRendererPreference(resolvedConfig, probe) {
  const requested = resolvedConfig && resolvedConfig.config
    ? resolvedConfig.config.renderer
    : undefined;
  let effective = requested;
  let trigger;
  if (probe && typeof probe === 'object' && RENDERER_VALUES.includes(probe.effective)) {
    effective = probe.effective;
    if (typeof probe.trigger === 'string') trigger = probe.trigger;
  }
  const out = { requested, effective, downgraded: effective !== requested };
  if (trigger !== undefined) out.trigger = trigger;
  return out;
}

function envNameFor(key) {
  return ENV_PREFIX + key.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toUpperCase();
}

// Parse one flat `key: value` line. Returns null for blanks/comments.
// Numeric and boolean scalars stay scalars so the type check below can
// reject them for string keys (no silent coercion into the schema).
function parseLine(line) {
  const trimmed = line.trim();
  if (!trimmed || trimmed.startsWith('#')) return null;
  const colon = trimmed.indexOf(':');
  if (colon === -1) return null;
  const key = trimmed.slice(0, colon).trim();
  let raw = trimmed.slice(colon + 1).trim();
  if (!key) return null;
  if (raw.length >= 2 && ((raw.startsWith('"') && raw.endsWith('"')) || (raw.startsWith("'") && raw.endsWith("'")))) {
    return { key, value: raw.slice(1, -1) };
  }
  if (raw === 'true') return { key, value: true };
  if (raw === 'false') return { key, value: false };
  if (/^-?\d+(\.\d+)?$/.test(raw)) return { key, value: Number(raw) };
  return { key, value: raw };
}

// Parse the collected `muse:` block text into nested plain objects.
// Minimal indentation-aware parse (no YAML dependency): a `key:` line with
// an empty value opens a nested map for the deeper-indented lines below it;
// a `key: value` line sets a scalar leaf (same scalar rules as parseLine).
// The leading top-level `muse:` header folds into the result, so the return
// shape is `{ muse: {...} }` ready for `validateMuseSection`. Shared with
// the build layer (T2); blank lines and `#` comments are ignored.
function parseMuseSubtree(museText, file) {
  const where = typeof file === 'string' && file ? file : 'muse-native.yaml';
  const root = {};
  const stack = [{ indent: -1, map: root }];
  for (const rawLine of String(museText).split('\n')) {
    const trimmed = rawLine.trim();
    if (!trimmed || trimmed.startsWith('#')) continue;
    const indent = rawLine.match(/^[ \t]*/)[0].length;
    const colon = trimmed.indexOf(':');
    if (colon === -1) continue;
    const key = trimmed.slice(0, colon).trim();
    if (!key) continue;
    // Prototype-pollution hardening: `__proto__`/`constructor`/`prototype`
    // must never flow into `parent[key] = ...` below. A `__proto__`
    // assignment mutates the fresh object's prototype instead of creating an
    // own key, so the key is silently dropped past validation (own-key
    // enumeration never sees it). Fail closed like the rest of this layer:
    // reject the key loudly instead of mis-parsing it. Valid keys are
    // unaffected — this guard only fires on the three dangerous names.
    if (key === '__proto__' || key === 'constructor' || key === 'prototype') {
      throw new ConfigError(key, where, 'rejected dangerous key');
    }
    while (stack.length > 1 && indent <= stack[stack.length - 1].indent) stack.pop();
    const parent = stack[stack.length - 1].map;
    const rest = trimmed.slice(colon + 1).trim();
    if (rest === '' || rest.startsWith('#')) {
      const child = {};
      parent[key] = child;
      stack.push({ indent, map: child });
    } else {
      const leaf = parseLine(trimmed);
      parent[key] = leaf ? leaf.value : rest;
    }
  }
  return root;
}

// Read the file layer: canonical `muse-native.yaml` wins when present;
// the deprecated aliases are a read-only fallback used only when the
// canonical file is absent. Returns { values, file, muse } where `file`
// is the basename of the file the values came from ('' when no file
// exists) and `muse` is the parsed `muse:` subtree (`{ muse: {...} }`,
// undefined when the file declares no `muse:` block) for the build layer.
//
// The init template ships a top-level `muse:` mapping (models/presets)
// the flat scalar parser cannot represent, so the `muse:` line and every
// line indented under it are routed out of the scalar `values` map before
// `validateConfig` (nested `models:`/`presets:` lines must never leak in as
// scalar keys). `muse` stays out of SCHEMA and the CLI/env layers:
// a structured section, not a scalar key.
function readFileLayer(cwd) {
  const candidates = [CANONICAL_FILE].concat(ALIAS_FILES);
  let found = null;
  for (const name of candidates) {
    let stat = null;
    try {
      stat = fs.statSync(path.join(cwd, name));
    } catch (_) {
      stat = null;
    }
    if (stat && stat.isFile()) {
      found = name;
      break;
    }
  }
  if (!found) return { values: {}, file: '', muse: undefined };
  const full = path.join(cwd, found);
  const text = fs.readFileSync(full, 'utf8');
  const values = {};
  const museLines = [];
  let inMuse = false;
  // T4 file form for the one map key: a top-level `diagnostics:` block with
  // an empty value opens a nested map for the deeper-indented lines below it
  // (`CAPO0012: warn`), and flat `diagnostics.CAPO0012: warn` leaves fold into
  // the same map. Either form lands in `values.diagnostics` as an object so
  // `validateConfig` per-code `error|warn|off` check can fire on real files.
  let inDiagnostics = false;
  for (const line of text.split('\n')) {
    if (inDiagnostics) {
      if (/^[ \t]/.test(line)) {
        const trimmedInner = line.trim();
        if (trimmedInner === '' || trimmedInner.startsWith('#')) continue;
        const parsedInner = parseLine(trimmedInner);
        if (parsedInner) {
          if (parsedInner.key === '__proto__' || parsedInner.key === 'constructor' || parsedInner.key === 'prototype') {
            throw new ConfigError(parsedInner.key, full, 'rejected dangerous key');
          }
          if (typeof values.diagnostics !== 'object' || values.diagnostics === null) values.diagnostics = {};
          values.diagnostics[parsedInner.key] = parsedInner.value;
        }
        continue;
      }
      inDiagnostics = false;
    }
    if (inMuse) {
      const trimmed = line.trim();
      if (trimmed === '' || trimmed.startsWith('#') || /^[ \t]/.test(line)) {
        museLines.push(line);
        continue;
      }
      inMuse = false;
    }
    const trimmed = line.trim();
    if (trimmed === '' || trimmed.startsWith('#')) continue;
    // Top-level `muse:` (no leading indent): route out of the scalar map.
    // An empty value opens the indented subtree; an inline value has no
    // subtree — either way `muse` is never a scalar config key.
    if (!/^[ \t]/.test(line) && /^muse\s*:/.test(trimmed)) {
      museLines.push(line);
      const rest = trimmed.slice(trimmed.indexOf(':') + 1).trim();
      if (rest === '' || rest.startsWith('#')) inMuse = true;
      continue;
    }
    // Top-level `diagnostics:` with an empty value opens the nested map;
    // an inline scalar value stays fail-closed via validateConfig below.
    if (!/^[ \t]/.test(line) && /^diagnostics\s*:/.test(trimmed)) {
      const rest = trimmed.slice(trimmed.indexOf(':') + 1).trim();
      if (rest === '' || rest.startsWith('#')) {
        if (typeof values.diagnostics !== 'object' || values.diagnostics === null) values.diagnostics = {};
        inDiagnostics = true;
        continue;
      }
    }
    const parsed = parseLine(line);
    if (parsed) {
      if (parsed.key === '__proto__' || parsed.key === 'constructor' || parsed.key === 'prototype') {
        throw new ConfigError(parsed.key, full, 'rejected dangerous key');
      }
      // FR-PACKAGING-009 T5 file-only pin: `sdk: <version>` validates
      // (string) but is routed out of the scalar `values` map before
      // `validateConfig`, exactly like the `muse:` subtree — it never
      // leaks into resolveConfig's SCHEMA-iterated merge.
      if (parsed.key === 'sdk') {
        if (typeof parsed.value !== 'string') {
          throw new ConfigError('sdk', full, 'string');
        }
        continue;
      }
      // Flat `diagnostics.CODE: <severity>` file form folds into the map.
      if (parsed.key === 'diagnostics' || parsed.key.startsWith('diagnostics.')) {
        if (parsed.key.startsWith('diagnostics.')) {
          const code = parsed.key.slice('diagnostics.'.length);
          if (code) {
            if (code === '__proto__' || code === 'constructor' || code === 'prototype') {
              throw new ConfigError(code, full, 'rejected dangerous key');
            }
            if (typeof values.diagnostics !== 'object' || values.diagnostics === null) values.diagnostics = {};
            values.diagnostics[code] = parsed.value;
            continue;
          }
        }
      }
      values[parsed.key] = parsed.key === 'archs' && typeof parsed.value === 'string'
        ? parseArchsValue(parsed.value)
        : parsed.value;
    }
  }
  return { values, file: full, muse: museLines.length ? parseMuseSubtree(museLines.join('\n'), full) : undefined };
}

// Boolean keys accept a bare `--<key>` flag meaning true (ASSUMPTION A1);
// value keys use `--<key> <value>` space form.
const BOOL_KEYS = new Set(['withSample', 'release', 'strict']);

// Normalize an env/CLI boolean-ish value to a real boolean when it names
// one (`true`/`false` in either string or boolean form); anything else
// passes through untouched so the merge/validation layer decides.
function normalizeBooleanValue(value) {
  if (value === true || value === 'true') return true;
  if (value === false || value === 'false') return false;
  return value;
}

// Collect `--<key> <value>` pairs from argv. Positional operands and the
// `--print-config` display flag are not config keys. A missing value or an
// unknown key throws ConfigError; never returns a partial map.
function readCliLayer(argv) {
  const values = {};
  const args = Array.isArray(argv) ? argv : [];
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (typeof arg !== 'string' || !arg.startsWith('--')) continue;
    if (arg === '--print-config') continue;
    const rawKey = arg.slice(2);
    // Accept kebab-case on the CLI (`--with-sample`) for camelCase keys.
    const key = rawKey.replace(/-([a-z])/g, (_, c) => c.toUpperCase());
    if (!Object.prototype.hasOwnProperty.call(SCHEMA, key)) {
      throw new ConfigError(key, '<argv>', 'unknown key, expected one of: ' + Object.keys(SCHEMA).join(', '));
    }
    if (BOOL_KEYS.has(key)) {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.startsWith('--')) {
        values[key] = true;
        continue;
      }
      if (next === 'true') {
        values[key] = true;
        i++;
        continue;
      }
      if (next === 'false') {
        values[key] = false;
        i++;
        continue;
      }
      // A bare boolean flag takes no value: a trailing non-flag token that
      // is not `true`/`false` is a typo'd value, not a positional. Fail
      // closed naming the option instead of silently dropping the token.
      throw new ConfigError(key, '<argv>', 'boolean');
    }
    const value = args[i + 1];
    if (typeof value !== 'string' || value.startsWith('--')) {
      throw new ConfigError(key, '<argv>', SCHEMA[key]);
    }
    values[key] = value;
    i++;
  }
  return values;
}

function readEnvLayer(env) {
  const values = {};
  const source = env && typeof env === 'object' ? env : {};
  for (const key of Object.keys(SCHEMA)) {
    const name = envNameFor(key);
    if (Object.prototype.hasOwnProperty.call(source, name) && source[name] !== undefined) {
      // Boolean keys resolve to real booleans so `config.strict === true`
      // (boolean) per the ResolvedConfig type; `isSampleSelected` keeps
      // accepting both forms downstream regardless.
      if (SCHEMA[key] === 'boolean') {
        values[key] = normalizeBooleanValue(source[name]);
      } else {
        values[key] = String(source[name]);
      }
    }
  }
  return values;
}

// Resolve the effective config. `argv` holds the args after `build`
// (positionals included; only `--<key> <value>` pairs are read), `env` is
// the environment map, `cwd` is the app dir holding the config file.
// Throws ConfigError on unknown keys / wrong types; never returns partial.
function resolveConfig(argv, env, cwd) {
  const dir = typeof cwd === 'string' && cwd ? cwd : process.cwd();
  const cliValues = readCliLayer(argv);
  const envValues = readEnvLayer(env);
  const fileLayer = readFileLayer(dir);
  const fileValues = fileLayer.values;
  const fileName = fileLayer.file || path.join(dir, CANONICAL_FILE);

  // Fail closed before merging: validate the file layer first (unknown
  // keys, then wrong types), then wrong types across the other layers.
  // Nothing is returned until all checks pass.
  validateConfig(fileValues, fileName);
  const layers = [
    { values: envValues, file: '<env>' },
    { values: cliValues, file: '<argv>' },
  ];
  for (const layer of layers) {
    for (const key of Object.keys(layer.values)) {
      const expected = SCHEMA[key];
      // T1: CSV/flow-list forms are accepted on the CLI/env layers too —
      // normalize to arrays before the type gate so enum validation below
      // sees the same shape as the file layer.
      if (key === 'archs' && typeof layer.values[key] === 'string') {
        layer.values[key] = parseArchsValue(layer.values[key]);
      }
      const actual = typeof layer.values[key];
      if (actual === expected) continue;
      // Env values arrive via String() and CLI booleans use bare flags:
      // allow the string passthrough for boolean/object keys across the
      // CLI/env layers (file layer stays strictly typed via validateConfig).
      if (expected === 'boolean' && actual === 'string') continue;
      if (expected === 'object' && actual === 'string') continue;
      throw new ConfigError(key, layer.file, expected);
    }
    // T1 enum validation per layer so CLI/env errors name the value +
    // accepted set at configure time, before anything merges.
    if (layer.values.archs !== undefined) assertArchsList(layer.values.archs, layer.file);
    if (layer.values.packaging !== undefined) assertPackagingValue(layer.values.packaging, layer.file);
    if (layer.values.renderer !== undefined) assertRendererValue(layer.values.renderer, layer.file);
  }

  const config = {};
  const winningLayer = {};
  for (const key of Object.keys(SCHEMA)) {
    if (Object.prototype.hasOwnProperty.call(cliValues, key)) {
      config[key] = SCHEMA[key] === 'boolean' ? normalizeBooleanValue(cliValues[key]) : cliValues[key];
      winningLayer[key] = 'cli';
    } else if (Object.prototype.hasOwnProperty.call(envValues, key)) {
      config[key] = SCHEMA[key] === 'boolean' ? normalizeBooleanValue(envValues[key]) : envValues[key];
      winningLayer[key] = 'env';
    } else if (Object.prototype.hasOwnProperty.call(fileValues, key)) {
      config[key] = fileValues[key];
      winningLayer[key] = 'file';
    } else {
      config[key] = key === 'archs' && Array.isArray(DEFAULTS[key]) ? DEFAULTS[key].slice() : DEFAULTS[key];
      winningLayer[key] = 'default';
    }
  }
  // T4 (AC-WORKFLOW-016): pre-probe preference pair. Carried as a top-level
  // `renderer` key so `--print-config` (which spreads the resolved object)
  // reports `{ requested, effective, downgraded }` with effective defaulting
  // to requested pre-probe. `config.renderer` itself is never mutated.
  const renderer = resolveRendererPreference({ config }, undefined);
  return { config, winningLayer, renderer };
}

// Option schema for `muse-native build --help`: derived from the same live
// SCHEMA/DEFAULTS objects validateConfig/resolveConfig check, so the help
// list cannot drift from validation. Build-only flags (e.g. `--stage`,
// stripped before resolveConfig — not a config key) append here with their
// live allowed values; config entries never duplicate the schema literals.
function getOptionSchema() {
  const entries = Object.keys(SCHEMA).map((name) => ({
    name,
    type: SCHEMA[name],
    default: DEFAULTS[name],
  }));
  for (const entry of entries) {
    if (entry.name === 'renderer') entry.allowed = RENDERER_VALUES.slice();
  }
  let stageAllowed = [];
  try {
    stageAllowed = require('./stages.js').STAGES;
  } catch (_) {
    stageAllowed = [];
  }
  entries.push({
    name: 'stage',
    type: 'string',
    default: undefined,
    allowed: Array.isArray(stageAllowed) ? stageAllowed.slice() : [],
  });
  return entries;
}

module.exports = { SCHEMA, DEFAULTS, ConfigError, resolveConfig, validateConfig, getOptionSchema, readFileLayer, parseMuseSubtree, assertRendererValue, resolveRendererPreference };
