'use strict';
// T5 — FR-DX-008 / AC-DX-037.
// BuildBackend facade: delegates every build invocation to the injected
// BUILD-001 implementation. No bundler/packaging internals live here.

function createBuildBackend(impl) {
  if (!impl || typeof impl.build !== 'function') {
    throw new TypeError('createBuildBackend requires an impl with a build function');
  }
  return {
    build: (opts) => impl.build(opts),
    invoke: (opts) => impl.build(opts),
    run: (opts) => impl.build(opts),
  };
}

let defaultImpl = null;

function __setBuildImpl(impl) {
  defaultImpl = impl;
}

function build(opts) {
  if (!defaultImpl || typeof defaultImpl.build !== 'function') {
    throw new Error('no BUILD-001 impl set; call __setBuildImpl(impl) first');
  }
  return defaultImpl.build(opts);
}

function invoke(opts) {
  return build(opts);
}

function run(opts) {
  return build(opts);
}

module.exports = { createBuildBackend, __setBuildImpl, build, invoke, run };
