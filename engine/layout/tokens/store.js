'use strict';
// FR-LAYOUT-009 / AC-LAYOUT-020 — token store + resolution cascade.
// Cascade order is defined once, here (CONFLICT-M3-002): component -> role ->
// theme -> default. The resolver always resolves and never throws on lookup
// input; unknown or empty token names fall to the `default` tier.

const COMPONENT_ROLES = {
  ProbeCard: 'probe-role',
  ProbePlain: 'probe-role',
};

const DEFAULT_TOKENS = {
  't.cascade.probe': 'default-tier',
  't.cascade.default-only': 'default-only-value',
};

const THEME_TOKENS = {
  'probe-dark': {
    't.cascade.probe': 'theme-tier',
  },
  'probe-light': {
    't.cascade.probe': 'light-tier',
  },
};

const ROLE_TOKENS = {
  'probe-role': {
    't.cascade.probe': 'role-tier',
  },
};

const COMPONENT_TOKENS = {
  ProbeCard: {
    't.cascade.probe': 'component-tier',
  },
};

function resolveToken(name, component, theme) {
  if (typeof name !== 'string' || name === '') {
    return { value: typeof name === 'string' ? name : '', source: 'default' };
  }
  const componentTable =
    typeof component === 'string' ? COMPONENT_TOKENS[component] : undefined;
  if (componentTable && Object.hasOwn(componentTable, name)) {
    return { value: componentTable[name], source: 'component' };
  }
  const role = typeof component === 'string' ? COMPONENT_ROLES[component] : undefined;
  const roleTable = typeof role === 'string' ? ROLE_TOKENS[role] : undefined;
  if (roleTable && Object.hasOwn(roleTable, name)) {
    return { value: roleTable[name], source: 'role' };
  }
  const themeTable = typeof theme === 'string' ? THEME_TOKENS[theme] : undefined;
  if (themeTable && Object.hasOwn(themeTable, name)) {
    return { value: themeTable[name], source: 'theme' };
  }
  if (Object.hasOwn(DEFAULT_TOKENS, name)) {
    return { value: DEFAULT_TOKENS[name], source: 'default' };
  }
  return { value: name, source: 'default' };
}

module.exports = { resolveToken };
