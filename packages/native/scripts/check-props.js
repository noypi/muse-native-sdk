'use strict';
// FR-M3-002 / AC-M3-004 + FR-M3-004 / AC-M3-008 — typed prop-schema gate (T6).
//
// Verifies that the named literal unions and prop declarations in
// packages/native/index.d.ts are generated from the manifest `values` lists
// in muse.manifest.json (manifest single source per RULE-M3-003), so types
// and manifest cannot drift:
//   - every manifest prop key is declared in the matching `<Name>Props` type
//     and vice versa (a removed schema prop breaks its usages at type check);
//   - every manifest `values` list equals the matching d.ts union (an
//     unsupported variant such as `variant="giant"` fails the gate);
//   - the named ButtonVariant / CardVariant / ChipVariant aliases equal the
//     Button / Card / Chip manifest `values` lists.
// Exits 0 on a match, 1 naming the offending `component.prop` otherwise.
//
// Testability (ASSUMPTION P3, mirrors ASSUMPTION G1 of check-manifest.js):
// input paths resolve from MANIFEST_PATH / DTS_PATH env vars when set,
// defaulting to the real manifest + index.d.ts next to this script. Fixtures
// use tmp-dir copies so production files are never mutated.

const fs = require('node:fs');
const path = require('node:path');

const MANIFEST_PATH =
  process.env.MANIFEST_PATH || path.join(__dirname, '..', 'muse.manifest.json');
const DTS_PATH =
  process.env.DTS_PATH || path.join(__dirname, '..', 'index.d.ts');

const VARIANT_ALIASES = {
  ButtonVariant: 'Button',
  CardVariant: 'Card',
  ChipVariant: 'Chip',
};

function fail(componentProp, detail) {
  console.error(`check-props: ${componentProp}${detail ? ` — ${detail}` : ''}`);
  process.exit(1);
}

function loadJson(jsonPath, label) {
  let raw;
  try {
    raw = fs.readFileSync(jsonPath, 'utf8');
  } catch (err) {
    console.error(`check-props: cannot read ${label} at ${jsonPath}: ${err.message}`);
    process.exit(1);
  }
  try {
    return JSON.parse(raw);
  } catch (err) {
    console.error(`check-props: ${label} at ${jsonPath} is not valid JSON: ${err.message}`);
    process.exit(1);
  }
}

function loadDts(dtsPath) {
  try {
    return fs.readFileSync(dtsPath, 'utf8');
  } catch (err) {
    console.error(`check-props: cannot read index.d.ts at ${dtsPath}: ${err.message}`);
    process.exit(1);
  }
}

function quotedLiterals(fragment) {
  const out = [];
  const re = /'([^']*)'|"([^"]*)"/g;
  let m;
  while ((m = re.exec(fragment)) !== null) out.push(m[1] !== undefined ? m[1] : m[2]);
  return out;
}

function sameStringSet(a, b) {
  if (a.length !== b.length) return false;
  const set = new Set(a);
  if (set.size !== a.length) return false;
  return b.every((v) => set.has(v));
}

function propsBody(dts, component) {
  const re = new RegExp(`export\\s+type\\s+${component}Props\\s*=\\s*\\{([^}]*)\\}`, 's');
  const m = re.exec(dts);
  return m ? m[1] : null;
}

function declaredPropNames(body) {
  const names = [];
  const re = /(\w+)\s*\??:/g;
  let m;
  while ((m = re.exec(body)) !== null) names.push(m[1]);
  return names;
}

function declaredPropFragment(body, prop) {
  const re = new RegExp(`${prop}\\s*\\??:\\s*([^;,}]+)`);
  const m = re.exec(body);
  return m ? m[1] : null;
}

function main() {
  const manifest = loadJson(MANIFEST_PATH, 'manifest');
  if (!manifest || !Array.isArray(manifest.components)) {
    console.error(`check-props: manifest at ${MANIFEST_PATH} must define a components array`);
    process.exit(1);
  }
  const dts = loadDts(DTS_PATH);

  // 1. Named *Variant aliases equal their manifest `values` lists.
  for (const [alias, component] of Object.entries(VARIANT_ALIASES)) {
    const re = new RegExp(`type\\s+${alias}\\s*=\\s*([^;]+);`, 's');
    const m = re.exec(dts);
    if (!m) fail(`${component}.variant`, `index.d.ts must declare a named ${alias} union type`);
    const entry = manifest.components.find((c) => c && c.name === component);
    const values = entry && entry.props && entry.props.variant && entry.props.variant.values;
    if (!Array.isArray(values)) fail(`${component}.variant`, 'manifest schema must declare variant values');
    if (!sameStringSet(quotedLiterals(m[1]), values)) {
      fail(`${component}.variant`, `d.ts ${alias} union drifts from the manifest values list`);
    }
  }

  // 2. Per-component prop keys + `values` unions match the manifest schemas.
  for (const entry of manifest.components) {
    const body = propsBody(dts, entry.name);
    if (body === null) fail(`${entry.name}.props`, `index.d.ts must declare a ${entry.name}Props type`);
    const manifestProps = Object.keys(entry.props ?? {});
    const declared = declaredPropNames(body);

    for (const prop of manifestProps) {
      if (!declared.includes(prop)) {
        fail(`${entry.name}.${prop}`, 'manifest schema prop missing from index.d.ts');
      }
    }
    for (const prop of declared) {
      if (!manifestProps.includes(prop)) {
        fail(`${entry.name}.${prop}`, 'index.d.ts declares a prop removed from the manifest schema');
      }
    }

    for (const [prop, schema] of Object.entries(entry.props ?? {})) {
      if (schema && Array.isArray(schema.values)) {
        const fragment = declaredPropFragment(body, prop);
        const union = fragment ? quotedLiterals(fragment) : [];
        // Resolve named-alias references (e.g. `variant?: ButtonVariant`).
        let resolved = union;
        if (resolved.length === 0 && fragment) {
          const aliasRef = Object.keys(VARIANT_ALIASES).find((a) => fragment.includes(a));
          if (aliasRef) {
            const aliasRe = new RegExp(`type\\s+${aliasRef}\\s*=\\s*([^;]+);`, 's');
            const aliasMatch = aliasRe.exec(dts);
            if (aliasMatch) resolved = quotedLiterals(aliasMatch[1]);
          }
        }
        if (!sameStringSet(resolved, schema.values)) {
          fail(`${entry.name}.${prop}`, 'index.d.ts union drifts from the manifest values list');
        }
      }
    }
  }

  const count = manifest.components.length;
  console.log(`check-props: OK — ${count} component schemas match index.d.ts`);
}

main();
