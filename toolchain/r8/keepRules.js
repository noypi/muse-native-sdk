'use strict';

// T8 — Generated (not hand-written) keep rules cover JNI/codegen/FlatBuffers/JSI/Hermes
// (NFR-BUILD-007 / AC-BUILD-012).
// Validator rejects hand-maintained input: generator must be a generator stamp,
// and every required surface token must appear in the rules text.

const REQUIRED_TOKENS = [
  'native-method',
  'jni-callback',
  'flatbuffer',
  'jsi-exposed',
  'hermes-reflection',
];

function isGeneratorStamp(generator) {
  if (typeof generator !== 'string') {
    return false;
  }
  if (generator.length === 0) {
    return false;
  }
  // Exact lowercase match: hand-maintained sources (hand/manual in any case)
  // are never generator stamps. HAND fails like hand.
  const normalized = generator.trim().toLowerCase();
  if (normalized === 'hand' || normalized === 'manual') {
    return false;
  }
  // Fail closed: only a generator-stamped provenance passes. Stamps contain
  // a generator marker (case-sensitive, per ASSUMPTION exact-lowercase match).
  return generator.includes('generat') || generator.includes('keepgen');
}

function keepRulesCover(generator, rules) {
  const text = typeof rules === 'string' ? rules : '';
  const missing = REQUIRED_TOKENS.filter((token) => !text.includes(token));
  const ok = isGeneratorStamp(generator) && missing.length === 0;
  return { ok, missing };
}

module.exports = { keepRulesCover };
