'use strict';

// Gallery renderer for JS-only packages (FR-PKG-009 / AC-PKG-009, T5).
// Engine primitives only: maps plain component data from the exports object
// resolved through `importPackage` onto `prim` without invoking any drawing
// code inside the package itself (AC-PKG-009/014 boundary).

const MAX_COMPONENT_JSON = 16384;

function fail(code, path, expected) {
  const err = new Error(`${code} at ${path}: expected ${expected}`);
  err.code = code;
  err.path = path;
  err.expected = expected;
  throw err;
}

function checkSize(component, path) {
  let json;
  try {
    json = JSON.stringify(component.geometry);
  } catch {
    fail('E_COMPONENT', path, 'component geometry must be JSON-serializable');
  }
  if (json.length > MAX_COMPONENT_JSON) {
    fail(
      'E_COMPONENT_SIZE',
      path,
      `component payload must be <= ${MAX_COMPONENT_JSON} bytes of JSON`,
    );
  }
}

// Render a JS-only package's components through engine primitives.
// Never calls into the package object; only reads `pkg.components`.
function renderJsPackage(pkg, prim) {
  if (!pkg || typeof pkg !== 'object' || !Array.isArray(pkg.components)) {
    fail('E_COMPONENT', 'components', 'package must expose a components array');
  }
  if (!prim || typeof prim.box !== 'function') {
    fail('E_PRIMITIVES', 'prim', 'engine-primitive factory must expose box()');
  }
  const out = [];
  for (const component of pkg.components) {
    const name =
      component && typeof component.name === 'string' && component.name.length > 0
        ? component.name
        : null;
    const path = `components/${name || 'unknown'}`;
    if (name === null) {
      fail('E_COMPONENT', path, 'each component must declare a non-empty name string');
    }
    if (!component.geometry || typeof component.geometry !== 'object') {
      fail('E_COMPONENT', path, 'each component must declare a geometry object');
    }
    checkSize(component, path);
    out.push(prim.box({ name, geometry: { ...component.geometry } }));
  }
  return out;
}

module.exports = { renderJsPackage };
