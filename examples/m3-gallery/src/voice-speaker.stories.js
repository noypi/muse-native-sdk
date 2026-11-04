'use strict';
// T6 gallery stories — MuseVoiceSpeaker bars default + one custom-figure story.
// Proves the AC-MUSE-033 preset and render-prop forms; TC-MUSE-032 manual half
// is documented as the procedure below (not executed in CI).
//
// TC-MUSE-032 manual procedure:
// 1. Render the bars Speaker and confirm all eight states
//    (idle|listening|loading|thinking|ready|error, plus denied/no-speech).
// 2. Render the custom-figure Speaker and confirm all eight states.
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

// TC-MUSE-031/032 glass leg (manual, device): ONE combined scripted story.
// Leg (a) — two rapid turns on one shared target: the newer turn commits
// first, the older commit drops (latest-committed-wins, stale-drop per the
// T1 turn.commit stale rule consumed here, never reimplemented).
// Leg (b) — active-mask switch mid-turn: partials stream, the sheet reroutes
// (route/latencyMs/reason update, text preserved), then commit binds targetId
// at commit time only — no write lands on the wrong target.
// Hermetic script: pure descriptor data only (no imports, no network); the
// manual half replays each op through openListeningSheet/commitSheet on glass.
const glassTwoTurnMaskSwitch = Object.freeze({
  name: 'MuseVoice / glass — two rapid turns + mid-turn mask switch',
  kind: 'glass-script',
  targetId: 'target-a',
  legs: Object.freeze([
    Object.freeze({
      leg: 'a-two-rapid-turns',
      ops: Object.freeze([
        { op: 'openSheet', turn: 'first', partials: ['hel'], final: 'first transcript' },
        { op: 'openSheet', turn: 'second', partials: ['hel'], final: 'second transcript' },
        { op: 'commit', turn: 'second', targetId: 'target-a', expectWrite: 'second transcript' },
        { op: 'commit', turn: 'first', targetId: 'target-a', expectWrite: null },
      ]),
      expect: Object.freeze({ targetValue: 'second transcript', staleDropped: 'first transcript' }),
    }),
    Object.freeze({
      leg: 'b-mask-switch-mid-turn',
      ops: Object.freeze([
        { op: 'openSheet', turn: 'third', route: 'transcribe', latencyMs: 5, reason: 'r' },
        { op: 'pushPartial', turn: 'third', text: 'hel' },
        { op: 'reroute', turn: 'third', route: 'transcribe→reason', latencyMs: 9, reason: 'dictation' },
        { op: 'setFinal', turn: 'third', final: 'hello world' },
        { op: 'commit', turn: 'third', targetId: 'target-a', expectWrite: 'hello world' },
      ]),
      expect: Object.freeze({
        partialsPreserved: ['hel'],
        route: 'transcribe→reason',
        targetValue: 'hello world',
        noWrongTargetWrite: true,
      }),
    }),
  ]),
  manualProcedure: Object.freeze([
    '1. Open two sheets (first, second) on target-a; commit second, then first; confirm target-a keeps "second transcript" (stale first dropped).',
    '2. Open a third sheet, push "hel", reroute to transcribe→reason, set final "hello world", commit to target-a; confirm partials preserved and no write lands on any other target.',
  ]),
});

const stories = {
  barsDefault: Object.fromEntries(STATUSES.map((s) => [s, barsStory(s)])),
  customFigure: Object.fromEntries(STATUSES.map((s) => [s, customFigureStory(s)])),
  glassTwoTurnMaskSwitch,
};

module.exports = { stories, barsStory, customFigureStory, STATUSES, glassTwoTurnMaskSwitch };
