'use strict';
// T2 — stub proxy: serves recorded SAM/Spark fixtures with no network and no key.
// Synchronous handle(req): { status, body }; throws a documented
// ProxyError (unknown-route) for unknown routes.

const { ProxyError } = require('./errors');

const FIXTURES = {
  // T5 — segment fixture: recorded SAM polygon plus suggestion polygons and a
  // reason-stream excerpt for the tap → mask → insight path. `mask`/`encoding`
  // are the pre-T5 minimal shape (kept for compatibility); `matches` mirrors
  // the dispatch-tested body shape consumed by the demo annotator.
  segment: {
    mask: 'polygon',
    encoding: 'polygon',
    matches: [
      {
        name: 'tap-region',
        insight: 'recorded-spark-insight',
        confidence: 0.9,
        mask: {
          encoding: 'polygon',
          points: [
            [360, 360],
            [408, 360],
            [408, 408],
            [360, 408],
          ],
        },
      },
    ],
    suggestions: [
      {
        encoding: 'polygon',
        dashed: true,
        points: [
          [330, 350],
          [370, 350],
          [370, 390],
          [330, 390],
        ],
      },
      {
        encoding: 'polygon',
        dashed: true,
        points: [
          [398, 398],
          [438, 398],
          [438, 438],
          [398, 438],
        ],
      },
      {
        encoding: 'polygon',
        dashed: true,
        points: [
          [360, 420],
          [400, 420],
          [400, 460],
          [360, 460],
        ],
      },
    ],
  },
  // T5 — reason fixture: recorded Spark reasoning plus the masked-insight
  // stream the demo Field renders for `context="masked:<label>:<photoId>"`.
  // T6 — onboarding turns mirror profile.ts transcript → profile reduction
  // (introduce → required-check → reprompt loop; volunteered detail lands in
  // optional custom info). The demo resolves turns through this route; the
  // reprompt strings match profile.ts repromptFor().
  reason: {
    reasoning: 'recorded-spark-reason',
    chunks: ['{"t":"rec"}'],
    streams: {
      masked: [
        '{"reasoning_content":"recorded-thinking"}',
        '{"content":"recorded-insight"}',
      ],
    },
    onboarding: {
      introduce: 'Tell me your name and favorite topic so I can set up your profile.',
      requiredCheck: ['displayName', 'favoriteTopic'],
      turns: [
        { match: 'my name is', fills: 'displayName' },
        { match: 'favorite topic is', fills: 'favoriteTopic' },
        { match: 'i love', fills: 'favoriteTopic' },
        { match: '3d', fills: 'artStyle' },
        { match: 'cartoon', fills: 'artStyle' },
      ],
      reprompts: {
        displayName: 'What should I call you?',
        favoriteTopic: 'What is your favorite topic?',
        both: 'What should I call you, and what is your favorite topic?',
      },
    },
  },
  // T5 — structure fixture: strict hierarchy per list renderMode. The demo
  // shelf reads `modes[<renderMode>]` so a mode switch restructures the
  // annotated set alone; `structure` is the pre-T5 minimal shape (kept).
  structure: {
    structure: 'recorded-spark-structure',
    strict: true,
    modes: {
      by_object: {
        summary: 'recorded summary by object',
        groups: [{ label: 'tap-region', sentiment: 'neutral', items: [{ id: 'a1', text: 'recorded' }] }],
      },
      by_photo: {
        summary: 'recorded summary by photo',
        groups: [{ label: 'photo-001', sentiment: 'neutral', items: [{ id: 'a1', text: 'recorded' }] }],
      },
      executive_summary: {
        summary: 'recorded executive summary',
        groups: [{ label: 'Summary', sentiment: 'neutral', items: [{ id: 'a1', text: 'recorded' }] }],
      },
    },
  },
  // T7 — transcribe fixture: recorded partials/final for standalone voice
  // entry plus the denied/no-speech terminal states. The demo resolves
  // partials → final through this route, then sends the final transcript to
  // `reason` for List-select enhancement (see
  // examples/muse-demo/src/transcribe.ts). Terminal markers mirror
  // DENIED_MARKER / NO_SPEECH_MARKER in that module.
  transcribe: {
    text: 'recorded-transcript',
    chunks: ['a'],
    partials: ['recorded-', 'recorded-trans'],
    final: 'recorded-transcript',
    denied: { terminal: 'denied', reason: 'microphone permission denied' },
    noSpeech: { terminal: 'no-speech', reason: 'no speech detected' },
  },
  // T6 — generate-image fixtures mirror avatar.tsx request shape (style
  // 3D-or-cartoon from profile.artStyle, size 512, seed profile.idHash,
  // cacheKey avatar:{profile.id}). Cache-first fetch wins; generate-image runs
  // only on 404/empty, async post-redirect, never blocking completion.
  // `image`/`size` are the pre-T6 minimal shape (kept for compatibility).
  'generate-image': {
    image: 'recorded-image-bytes',
    size: '1024x1024',
    generated: {
      uri: 'generated:avatar:local-user:cartoon:512',
      moderated: false,
      style: ['3d', 'cartoon'],
      size: 512,
      cacheKey: 'avatar:local-user',
    },
    moderated: { uri: 'generated:avatar:local-user:cartoon:512', moderated: true },
    photoPick: { uri: 'photo://onboarding-pick', moderated: true },
  },
};

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function handle(req) {
  const route = req && typeof req === 'object' ? req.route : undefined;
  if (typeof route !== 'string' || !Object.prototype.hasOwnProperty.call(FIXTURES, route)) {
    throw new ProxyError('unknown-route', String(route), false);
  }
  return { status: 200, body: clone(FIXTURES[route]) };
}

module.exports = { handle };
