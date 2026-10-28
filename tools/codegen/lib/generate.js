'use strict';
// T10 — FR-DX-005 / AC-DX-020 / TC-DX-020.
// Emits one TypeScript declaration file per manifest component.

const HEADER = '/* AUTO-GENERATED — do not hand-edit (DX-005) */';

const BLOCKED_NAMES = new Set(['__proto__', 'constructor', 'prototype']);
const IDENT = /^[A-Za-z_$][A-Za-z0-9_$]*$/;

function fail(code, cause, action) {
  const err = new Error(cause);
  err.code = code;
  err.cause = cause;
  err.action = action;
  throw err;
}

function mapType(type, diagnostics, where) {
  switch (type) {
    case 'string':
    case 'boolean':
    case 'number':
    case 'bigint':
    case 'object':
    case 'void':
    case 'unknown':
    case 'any':
    case 'never':
      return type;
    case 'token':
      return 'string';
    case undefined:
    case null:
    case '':
      return 'unknown';
    default: {
      if (Array.isArray(diagnostics)) {
        diagnostics.push({
          code: 'E_CODEGEN_UNKNOWN_TYPE',
          cause: `DX-005: unknown type ${JSON.stringify(String(type))}${where ? ` for ${where}` : ''} mapped to unknown`,
          action: 'Use a known DX-005 type (string, boolean, number, object, void, token, unknown) in the manifest.',
        });
      }
      return 'unknown';
    }
  }
}

function checkName(name) {
  if (typeof name !== 'string' || name.length === 0 || !IDENT.test(name) || BLOCKED_NAMES.has(name)) {
    fail(
      'E_CODEGEN_BAD_COMPONENT',
      `DX-005: invalid component name ${JSON.stringify(String(name))}`,
      'Rename the component to a valid module identifier and re-run the DX-005 generator.',
    );
  }
}

function renderComponent(component, diagnostics) {
  checkName(component.name);
  const name = component.name;
  const props = component.props && typeof component.props === 'object' ? component.props : {};
  const methods = Array.isArray(component.methods) ? component.methods : [];
  const events = Array.isArray(component.events) ? component.events : [];
  const enums = Array.isArray(component.enums) ? component.enums : [];

  const lines = [];
  lines.push(HEADER);
  lines.push(`/** Declaration for \`${name}\` (DX-005). */`);
  lines.push(`export interface ${name}Props {`);
  const propNames = Object.keys(props);
  if (propNames.length === 0) {
    lines.push(`  /** No props declared for \`${name}\` (DX-005 stub). */`);
    lines.push('  readonly __kind?: string;');
  }
  for (const propName of propNames) {
    if (!IDENT.test(propName) || BLOCKED_NAMES.has(propName)) {
      fail(
        'E_CODEGEN_BAD_PROP',
        `DX-005: invalid prop name ${JSON.stringify(propName)} on component ${JSON.stringify(name)}`,
        'Rename the prop to a valid identifier and re-run the DX-005 generator.',
      );
    }
    const def = props[propName] && typeof props[propName] === 'object' ? props[propName] : {};
    const optional = def.required === true ? '' : '?';
    lines.push(`  /** Prop \`${propName}\` of \`${name}\`. */`);
    lines.push(`  ${propName}${optional}: ${mapType(def.type, diagnostics, `prop ${propName} on ${name}`)};`);
  }
  lines.push('}');
  lines.push('');
  for (const method of methods) {
    const mName = method && method.name;
    if (typeof mName !== 'string' || !IDENT.test(mName) || BLOCKED_NAMES.has(mName)) {
      fail(
        'E_CODEGEN_BAD_METHOD',
        `DX-005: invalid method name ${JSON.stringify(String(mName))} on component ${JSON.stringify(name)}`,
        'Rename the method to a valid identifier and re-run the DX-005 generator.',
      );
    }
    const params = Array.isArray(method.params) ? method.params : [];
    const rendered = params.map((p, i) => `${IDENT.test(String(p)) && !BLOCKED_NAMES.has(String(p)) ? String(p) : `arg${i}`}: any`).join(', ');
    lines.push(`/** Method \`${mName}\` of \`${name}\`. */`);
    lines.push(`export function ${mName}(${rendered}): ${mapType(method.returns, diagnostics, `method ${mName} on ${name}`)};`);
    lines.push('');
  }
  for (const event of events) {
    const eName = event && event.name;
    if (typeof eName !== 'string' || !IDENT.test(eName) || BLOCKED_NAMES.has(eName)) {
      fail(
        'E_CODEGEN_BAD_EVENT',
        `DX-005: invalid event name ${JSON.stringify(String(eName))} on component ${JSON.stringify(name)}`,
        'Rename the event to a valid identifier and re-run the DX-005 generator.',
      );
    }
    lines.push(`/** Event \`${eName}\` of \`${name}\`. */`);
    lines.push(`export const ${eName}: (payload: ${mapType(event.payload, diagnostics, `event ${eName} on ${name}`)}) => void;`);
    lines.push('');
  }
  for (const en of enums) {
    const eName = en && en.name;
    const values = en && Array.isArray(en.values) ? en.values : null;
    if (typeof eName !== 'string' || !IDENT.test(eName) || BLOCKED_NAMES.has(eName) || !values) {
      fail(
        'E_CODEGEN_BAD_ENUM',
        `DX-005: invalid enum ${JSON.stringify(String(eName))} on component ${JSON.stringify(name)}`,
        'Fix the enum descriptor (name plus values array) and re-run the DX-005 generator.',
      );
    }
    const union = values.map((v) => JSON.stringify(String(v))).join(' | ');
    lines.push(`/** Enum \`${eName}\` of \`${name}\`. */`);
    lines.push(`export type ${eName} = ${union || 'never'};`);
    lines.push('');
  }
  lines.push(`/** Typed descriptor for \`${name}\` (DX-005). */`);
  lines.push(`export const ${name}: { props: ${name}Props };`);
  lines.push('');
  return lines.join('\n');
}

function generateDeclarations(manifest) {
  if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest)) {
    fail(
      'E_CODEGEN_BAD_MANIFEST',
      'DX-005: manifest must be an object with a components array',
      'Pass a manifest envelope shaped { components: [{ name, props?, methods?, events?, enums? }] } to generateDeclarations.',
    );
  }
  if (!Array.isArray(manifest.components)) {
    fail(
      'E_CODEGEN_BAD_MANIFEST',
      'DX-005: manifest.components must be an array',
      'Pass a manifest envelope shaped { components: [{ name, props?, methods?, events?, enums? }] } to generateDeclarations.',
    );
  }
  const files = Object.create(null);
  const diagnostics = [];
  for (const component of manifest.components) {
    if (!component || typeof component !== 'object') {
      fail(
        'E_CODEGEN_BAD_MANIFEST',
        'DX-005: every manifest.components entry must be an object with a name',
        'Fix the malformed component entry and re-run the DX-005 generator.',
      );
    }
    const text = renderComponent(component, diagnostics);
    files[component.name] = text;
  }
  return { files: { ...files }, diagnostics };
}

module.exports = { generateDeclarations, HEADER };
