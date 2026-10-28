'use strict';
// T6 story scaffolding (TC-MUSE-013 evidence): one error story per proxy
// taxonomy code for MuseField. Story descriptors only — no component
// implementation. Error state keeps existing text intact.

const TEXT = 'fever for three days';

const AUTH = {
  name: 'MuseField / error (auth)',
  state: 'error',
  props: { value: TEXT, context: 'medical_symptoms', privacy: 'auto', prompt: 'Summarize these symptoms.' },
  error: { code: 'auth', detail: 'stubbed auth failure' },
};

const UPSTREAM = {
  name: 'MuseField / error (upstream)',
  state: 'error',
  props: { value: TEXT, context: 'medical_symptoms', privacy: 'auto', prompt: 'Summarize these symptoms.' },
  error: { code: 'upstream', detail: 'stubbed upstream failure' },
};

const TIMEOUT = {
  name: 'MuseField / error (timeout)',
  state: 'error',
  props: { value: TEXT, context: 'medical_symptoms', privacy: 'auto', prompt: 'Summarize these symptoms.' },
  error: { code: 'timeout', detail: 'stubbed timeout' },
};

const SCHEMA = {
  name: 'MuseField / error (schema)',
  state: 'error',
  props: { value: TEXT, context: 'medical_symptoms', privacy: 'auto', prompt: 'Summarize these symptoms.' },
  error: { code: 'schema', detail: 'stubbed schema violation' },
};

module.exports = { stories: [AUTH, UPSTREAM, TIMEOUT, SCHEMA] };
