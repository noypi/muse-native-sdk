'use strict';
// T3 story scaffolding (TC-MUSE-010 evidence): loading / error / empty-mask
// stories for MuseLens. Story descriptors only — no component implementation.

const LOADING = {
  name: 'MuseLens / loading',
  state: 'loading',
  props: { imageUrl: 'stub://kitchen', prompt: 'Name this object.' },
};

const ERROR = {
  name: 'MuseLens / error (upstream)',
  state: 'error',
  props: { imageUrl: 'stub://kitchen', prompt: 'Name this object.' },
  error: { code: 'upstream', detail: 'stubbed upstream failure' },
};

const EMPTY_MASK = {
  name: 'MuseLens / empty-mask',
  state: 'empty-mask',
  props: { imageUrl: 'stub://kitchen', prompt: 'Name this object.' },
  error: { code: 'schema', reason: 'unparseable-mask' },
};

module.exports = { stories: [LOADING, ERROR, EMPTY_MASK] };
