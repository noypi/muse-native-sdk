'use strict';
// @muse/cli — muse: section validator for muse-native.yaml (T3).
// Dedicated owner of the `muse:` top-level mapping; the flat parser in
// `./config.js` is untouched and never sees this section. Runs at build time
// against the T1 proxy allow-list (`loadConfig` return: { version, routes })
// before any compile/stage work. A missing `muse:` section is valid
// ({ ok: true }); failing closed on absence would break existing projects.
// Anything present is validated fail-closed via ConfigError.

const FILE = 'muse-native.yaml';

// Error thrown for unlisted params, unknown preset routes, and non-Muse or
// route-mismatched models. Carries the offending key, the file the bad value
// came from, and a helpful expected string naming the valid value.
class ConfigError extends Error {
  constructor(key, file, expected) {
    super(`Error: ${key} in ${file}: expected ${expected}`);
    this.name = 'ConfigError';
    this.key = key;
    this.file = file;
    this.expected = expected;
  }
}

function isRecord(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

// Validate the `muse:` section of a parsed muse-native.yaml object against
// the proxy allow-list. Returns { ok: true }; throws ConfigError naming the
// key, file, and expected value on the first violation. An absent `preset`
// resolves to the route default (none — compiler default, no new surface),
// so models-only yaml and a missing `muse:` section both validate clean.
function validateMuseSection(yaml, allowList) {
  const muse = yaml && typeof yaml === 'object' ? yaml.muse : undefined;
  if (muse === undefined) return { ok: true };
  if (!isRecord(muse)) {
    throw new ConfigError('muse', FILE, 'object with optional models/presets mappings');
  }
  const routes = allowList && isRecord(allowList.routes) ? allowList.routes : {};
  const museModels = new Set(
    Object.keys(routes)
      .map((name) => routes[name] && routes[name].model)
      .filter((model) => typeof model === 'string'),
  );

  if (muse.models !== undefined) {
    if (!isRecord(muse.models)) {
      throw new ConfigError('muse.models', FILE, 'object mapping route to verified model ID');
    }
    for (const route of Object.keys(muse.models)) {
      if (!Object.prototype.hasOwnProperty.call(routes, route)) {
        throw new ConfigError(route, FILE, 'unknown route, expected one of: ' + Object.keys(routes).join(', '));
      }
      const model = muse.models[route];
      if (!museModels.has(model)) {
        const expected = `verified model for route '${route}', expected one of: ${[...museModels].join(', ')}`;
        // Non-Muse IDs name the model; a verified ID on the wrong route
        // names the route so the mismatch is locatable either way.
        throw new ConfigError(museModels.has(model) ? route : String(model), FILE, expected);
      }
    }
  }

  if (muse.presets !== undefined) {
    if (!isRecord(muse.presets)) {
      throw new ConfigError('muse.presets', FILE, 'object mapping preset name to { route, params }');
    }
    for (const name of Object.keys(muse.presets)) {
      const preset = muse.presets[name];
      if (!isRecord(preset) || typeof preset.route !== 'string') {
        throw new ConfigError(`muse.presets.${name}.route`, FILE, 'a known route name');
      }
      if (!Object.prototype.hasOwnProperty.call(routes, preset.route)) {
        throw new ConfigError(preset.route, FILE, 'unknown route, expected one of: ' + Object.keys(routes).join(', '));
      }
      if (preset.params !== undefined) {
        if (!isRecord(preset.params)) {
          throw new ConfigError(`muse.presets.${name}.params`, FILE, 'object');
        }
        const allowed = Object.keys(routes[preset.route].params || {});
        for (const param of Object.keys(preset.params)) {
          if (!allowed.includes(param)) {
            throw new ConfigError(param, FILE, `unlisted param for route '${preset.route}', expected one of: ${allowed.join(', ') || '(none)'}`);
          }
        }
      }
    }
  }

  return { ok: true };
}

// Resolve the validated `muse:` section to the value object the build
// persists. Reuses `validateMuseSection` checks (fail-closed gate) so any
// future widening of the validator flows through; returns the resolved
// `{ models, presets }` instead of `{ ok: true }`. Values come from the
// yaml verbatim (allow-lists are reject-only, never substituted). An absent
// `muse:` section resolves to empty maps; absent `muse.presets` to `{}`.
function resolveMuseSection(yaml, allowList) {
  validateMuseSection(yaml, allowList);
  const muse = yaml && typeof yaml === 'object' ? yaml.muse : undefined;
  if (muse === undefined) return { models: {}, presets: {} };
  const models = muse.models !== undefined ? { ...muse.models } : {};
  let presets = {};
  if (muse.presets !== undefined) {
    presets = {};
    for (const name of Object.keys(muse.presets)) {
      const preset = muse.presets[name];
      presets[name] = { route: preset.route, params: { ...(preset.params || {}) } };
    }
  }
  return { models, presets };
}

module.exports = { validateMuseSection, resolveMuseSection, ConfigError };
