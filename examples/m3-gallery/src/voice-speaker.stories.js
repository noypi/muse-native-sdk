'use strict';
// T6 gallery stories — MuseVoiceSpeaker bars default + one custom-figure story.
// Proves the AC-MUSE-033 preset and render-prop forms; TC-MUSE-032 manual half
// is documented as the procedure below (not executed in CI).
//
// TC-MUSE-032 manual procedure:
// 1. Render the bars Speaker and confirm all six states
//    (idle|listening|loading|thinking|ready|error, plus denied/no-speech).
// 2. Render the custom-figure Speaker and confirm all six states.
// 3. Confirm children overrides preset props (custom figure wins, dev warning
//    only — never a device error).
//
// Hermetic: pure descriptor objects only; no imports, no network.

function barsStory(status) {
  return {
    speaker: 'MuseVoiceSpeaker',
    form: 'preset',
    waveform: 'bars',
    showPartials: true,
    showBadge: true,
    state: {
      status,
      envelope: 0.5,
      partial: 'hello',
      route: 'transcribe→reason',
      latencyMs: 42,
      reason: 'dictation',
    },
  };
}

function customFigureStory(status) {
  return {
    speaker: 'MuseVoiceSpeaker',
    form: 'render-prop',
    state: {
      status,
      envelope: 0.5,
      partial: 'hello',
      route: 'transcribe→reason',
      latencyMs: 42,
      reason: 'dictation',
    },
    children: '(state) => ({ kind: "custom-figure", amplitude: state.envelope })',
  };
}

const STATUSES = ['idle', 'listening', 'loading', 'thinking', 'ready', 'error', 'denied', 'no-speech'];

const stories = {
  barsDefault: Object.fromEntries(STATUSES.map((s) => [s, barsStory(s)])),
  customFigure: Object.fromEntries(STATUSES.map((s) => [s, customFigureStory(s)])),
};

module.exports = { stories, barsStory, customFigureStory, STATUSES };
