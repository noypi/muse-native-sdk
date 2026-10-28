'use strict';
// T6 — upstream exclusivity audit: no non-Muse model declared anywhere,
// plus docs/meta-first.md exclusivity + perf-disposition claims.
// Hermetic: node:test + node:assert/strict + node:fs/path only — reads only
// the repo's own config/doc files, no network, no secrets, no live services.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const REPO_ROOT = path.join(__dirname, '..', '..');
const DOC = path.join(REPO_ROOT, 'docs', 'meta-first.md');
const PROXY_CONFIG = path.join(REPO_ROOT, 'proxy', 'proxy.config.json');
const YAML_TEMPLATE = path.join(REPO_ROOT, 'packages', 'cli', 'templates', 'muse-native.yaml');

const ALLOW_MODELS = [
  'sam-3.1',
  'muse-spark-1.3',
  'muse-voice-transcribe-1.0',
  'muse-image',
];

// Non-Muse model markers. ASSUMPTION: docs/meta-first.md states exclusivity
// without naming third-party models, so ANY occurrence of these markers in
// the scanned config/doc surface is a violation (fail-closed per plan).
const FORBIDDEN = [
  'gpt-',
  'gpt4',
  'gpt_4',
  'claude',
  'gemini',
  'llama',
  'mistral',
  'grok',
  'whisper',
  'dall-e',
  'dalle',
  'midjourney',
  'stable-diffusion',
  'openai',
  'anthropic',
  'meta-seamless',
  'meta-tts',
  'deepseek',
  'qwen',
  'falcon',
  'palm',
];

function read(p) {
  assert.ok(fs.existsSync(p), `expected file to exist: ${path.relative(REPO_ROOT, p)}`);
  return fs.readFileSync(p, 'utf8');
}

describe('T6 — docs/meta-first.md exclusivity statement (AC-MUSE-019, AC-MUSE-021)', () => {
  it('doc exists and states RULE-MUSE-006 exclusivity', () => {
    const src = read(DOC);
    assert.ok(/RULE-MUSE-006/.test(src), 'doc must cite RULE-MUSE-006');
    assert.ok(/exclusiv/i.test(src), 'doc must state exclusivity');
  });

  it('doc lists the closed allow-list of four Muse models', () => {
    const src = read(DOC);
    for (const m of ALLOW_MODELS) {
      assert.ok(
        src.includes(m),
        `doc must list allow-listed model ${JSON.stringify(m)}`,
      );
    }
  });

  it('doc states the spec-change-only fallback rule', () => {
    const src = read(DOC);
    assert.ok(
      /spec-change-only|spec change only/i.test(src),
      'doc must state the spec-change-only fallback rule',
    );
    assert.ok(/fallback/i.test(src), 'doc must describe the fallback rule');
  });
});

describe('T6 — perf-budget disposition (AC-MUSE-022, dep SPEC-PERFORMANCE)', () => {
  it('perf section references SPEC-PERFORMANCE budgets', () => {
    const src = read(DOC);
    assert.ok(/SPEC-PERFORMANCE/.test(src), 'doc must reference SPEC-PERFORMANCE');
    assert.ok(/AC-MUSE-022/.test(src), 'doc must cite AC-MUSE-022');
  });

  it('perf table marks routes measured or degraded (initially unmeasured/degraded)', () => {
    const src = read(DOC);
    assert.ok(
      /measured-or-degraded|measured or degraded/i.test(src),
      'doc must record the measured-or-degraded table rule',
    );
    assert.ok(
      /unmeasured|degraded/i.test(src),
      'doc must mark unmeasured routes degraded pending device-lab',
    );
  });
});

describe('T6 — no non-Muse model declared in config/docs (AC-MUSE-019, AC-MUSE-020)', () => {
  it('proxy.config.json declares only allow-listed Muse models', () => {
    const raw = read(PROXY_CONFIG);
    const cfg = JSON.parse(raw);
    assert.ok(cfg.routes && typeof cfg.routes === 'object', 'config must have a routes table');
    for (const [route, entry] of Object.entries(cfg.routes)) {
      assert.ok(
        ALLOW_MODELS.includes(entry.model),
        `route ${JSON.stringify(route)} declares non-Muse model ${JSON.stringify(entry.model)}`,
      );
    }
  });

  it('no forbidden non-Muse model string appears in config/docs surface', () => {
    const surfaces = [read(PROXY_CONFIG), read(DOC)];
    if (fs.existsSync(YAML_TEMPLATE)) {
      surfaces.push(fs.readFileSync(YAML_TEMPLATE, 'utf8'));
    }
    const hay = surfaces.join('\n').toLowerCase();
    for (const marker of FORBIDDEN) {
      assert.ok(
        !hay.includes(marker.toLowerCase()),
        `non-Muse model marker ${JSON.stringify(marker)} must not appear in config/docs`,
      );
    }
  });

  it('muse-native.yaml template models (when present) are allow-listed Muse IDs', () => {
    if (!fs.existsSync(YAML_TEMPLATE)) {
      return;
    }
    const src = fs.readFileSync(YAML_TEMPLATE, 'utf8');
    const ids = src.match(/(?<![A-Z0-9]-)[a-z0-9][a-z0-9._-]*-[0-9][a-z0-9.]*/gi) || [];
    const modelish = ids.filter((t) => /^(sam|muse)-/i.test(t) || /gpt|claude|gemini|llama|mistral|grok|whisper|dall|seamless|tts/i.test(t));
    for (const t of modelish) {
      assert.ok(
        ALLOW_MODELS.includes(t),
        `template declares non-Muse model ${JSON.stringify(t)}`,
      );
    }
  });
});
