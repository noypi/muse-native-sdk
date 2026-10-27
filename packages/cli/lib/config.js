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

const SCHEMA = {
  name: 'string',
  version: 'string',
};

const DEFAULTS = {
  name: 'hello-app',
  version: '0.0.1',
};

const ENV_PREFIX = 'MUSE_NATIVE_';

const CANONICAL_FILE = 'muse-native.yaml';
const ALIAS_FILES = ['muse.config.ts', 'capo.config.ts'];

// Error thrown for unknown keys / wrong value types. Carries the key,
// the file the bad value came from, and the expected type name.
class ConfigError extends Error {
  constructor(key, file, expected) {
    super(`Error: ${key} in ${file}: expected ${expected}`);
    this.name = 'ConfigError';
    this.key = key;
    this.file = file;
    this.expected = expected;
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
    if (!Object.prototype.hasOwnProperty.call(SCHEMA, key)) {
      throw new ConfigError(key, where, 'unknown key, expected one of: ' + Object.keys(SCHEMA).join(', '));
    }
  }
  for (const key of Object.keys(raw)) {
    const expected = SCHEMA[key];
    if (typeof raw[key] !== expected) {
      throw new ConfigError(key, where, expected);
    }
  }
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

// Read the file layer: canonical `muse-native.yaml` wins when present;
// the deprecated aliases are a read-only fallback used only when the
// canonical file is absent. Returns { values, file } where `file` is the
// basename of the file the values came from ('' when no file exists).
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
  if (!found) return { values: {}, file: '' };
  const full = path.join(cwd, found);
  const text = fs.readFileSync(full, 'utf8');
  const values = {};
  for (const line of text.split('\n')) {
    const parsed = parseLine(line);
    if (parsed) values[parsed.key] = parsed.value;
  }
  return { values, file: full };
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
    const key = arg.slice(2);
    if (!Object.prototype.hasOwnProperty.call(SCHEMA, key)) {
      throw new ConfigError(key, '<argv>', 'unknown key, expected one of: ' + Object.keys(SCHEMA).join(', '));
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
      values[key] = String(source[name]);
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
      if (typeof layer.values[key] !== expected) {
        throw new ConfigError(key, layer.file, expected);
      }
    }
  }

  const config = {};
  const winningLayer = {};
  for (const key of Object.keys(SCHEMA)) {
    if (Object.prototype.hasOwnProperty.call(cliValues, key)) {
      config[key] = cliValues[key];
      winningLayer[key] = 'cli';
    } else if (Object.prototype.hasOwnProperty.call(envValues, key)) {
      config[key] = envValues[key];
      winningLayer[key] = 'env';
    } else if (Object.prototype.hasOwnProperty.call(fileValues, key)) {
      config[key] = fileValues[key];
      winningLayer[key] = 'file';
    } else {
      config[key] = DEFAULTS[key];
      winningLayer[key] = 'default';
    }
  }
  return { config, winningLayer };
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

module.exports = { SCHEMA, DEFAULTS, ConfigError, resolveConfig, validateConfig, getOptionSchema };
