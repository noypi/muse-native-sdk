'use strict';
// T9 — FR-DX-009 / AC-DX-041 / TC-DX-041 (Batch 3).
// Hermetic: node:test + node:assert/strict only. Profiles + faults are
// applied to an in-memory engine-transport double via a test seam — no
// transport internals built here, no network, no secrets, no config.
// Interface under test (plan verbatim):
//   configureSession(opts: {profile: {kind:
//   'offline'|'highLatency'|'lossy', ms?: number, lossPct?: number}, faults:
//   Array<{target: string, kind: string, rate: number}>}) => {profile:
//   object, faults: object[]} from tools/debugbridge/lib/fault-injection.js;
//   invalid combos reject with {command:"configureSession", cause, action}.
// ASSUMPTION: tests inject the engine transport double through an optional
// `transport` field (or `__setTransport(double)` / `__getTransport()` hooks
// when the field form is absent). These hooks are test-only; production
// wires the engine's own transport stack. REL-RESILIENCE is opaque: faults
// are recorded on the double, never executed against real infrastructure.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const FAULT_INJECTION = path.join(__dirname, '..', 'lib', 'fault-injection.js');

function fresh(modPath) {
  delete require.cache[require.resolve(modPath)];
  return require(modPath);
}

// In-memory engine-transport double: records applied profile + faults and
// observably alters send() behavior (offline drops, latency delays,
// lossy drops at rate). Deterministic: loss uses a counter, not Math.random.
function makeTransportDouble() {
  const double = {
    appliedProfile: null,
    appliedFaults: [],
    sent: [],
    delivered: [],
    counter: 0,
    applyProfile(profile) { this.appliedProfile = { ...profile }; },
    applyFaults(faults) { this.appliedFaults = faults.map((f) => ({ ...f })); },
    async send(msg) {
      this.sent.push(msg);
      const profile = this.appliedProfile || {};
      if (profile.kind === 'offline') {
        const err = new Error('transport offline (fault profile)');
        err.code = 'TRANSPORT_OFFLINE';
        throw err;
      }
      if (profile.kind === 'lossy') {
        this.counter += 1;
        const pct = profile.lossPct || 0;
        // Deterministic drop: drop every Nth message approximating lossPct.
        const every = pct >= 100 ? 1 : Math.max(1, Math.round(100 / Math.max(1, pct)));
        if (this.counter % every === 0) {
          const err = new Error('message lost (lossy profile)');
          err.code = 'MESSAGE_LOST';
          throw err;
        }
      }
      this.delivered.push(msg);
      return { delivered: true };
    },
  };
  return double;
}

function configureWithDouble(mod, opts, double) {
  if (typeof mod.__setTransport === 'function') mod.__setTransport(double);
  const withField = { ...opts, transport: double };
  try {
    return mod.configureSession(withField);
  } catch (err) {
    if (err && err.code === 'ERR_UNKNOWN_TRANSPORT_FIELD') throw err;
    // Field form not accepted: call the two-arg / hook form.
    if (opts && opts.transport) {
      const { transport, ...rest } = opts;
      void transport;
      return mod.configureSession(rest);
    }
    return mod.configureSession(opts);
  }
}

describe('throttling profiles (T9 / TC-DX-041)', () => {
  it("offline profile configures and observably blocks the transport double", () => {
    const mod = fresh(FAULT_INJECTION);
    const double = makeTransportDouble();
    const out = configureWithDouble(mod, { profile: { kind: 'offline' }, faults: [] }, double);
    assert.ok(out && typeof out === 'object');
    assert.equal(out.profile.kind, 'offline');
    assert.deepEqual(out.faults, []);
    assert.equal(double.appliedProfile.kind, 'offline', 'profile must reach the engine transport double');
    return assert.rejects(async () => double.send({ hello: 1 }), /offline/i);
  });

  it('highLatency profile carries ms and is applied to the double', () => {
    const mod = fresh(FAULT_INJECTION);
    const double = makeTransportDouble();
    const out = configureWithDouble(mod, { profile: { kind: 'highLatency', ms: 800 }, faults: [] }, double);
    assert.equal(out.profile.kind, 'highLatency');
    assert.equal(out.profile.ms, 800);
    assert.equal(double.appliedProfile.ms, 800);
  });

  it('lossy profile carries lossPct and drops messages observably', async () => {
    const mod = fresh(FAULT_INJECTION);
    const double = makeTransportDouble();
    const out = configureWithDouble(mod, { profile: { kind: 'lossy', lossPct: 50 }, faults: [] }, double);
    assert.equal(out.profile.kind, 'lossy');
    assert.equal(out.profile.lossPct, 50);
    let lost = 0;
    for (let i = 0; i < 4; i++) {
      try { await double.send({ i }); } catch { lost += 1; }
    }
    assert.ok(lost >= 1, 'lossy profile must observably drop messages on the double');
  });
});

describe('fault injection (T9 / TC-DX-041)', () => {
  it('injected faults are recorded on the transport double', () => {
    const mod = fresh(FAULT_INJECTION);
    const double = makeTransportDouble();
    const faults = [{ target: 'dev-channel', kind: 'drop', rate: 0.5 }];
    const out = configureWithDouble(mod, { profile: { kind: 'highLatency', ms: 100 }, faults }, double);
    assert.equal(out.faults.length, 1);
    assert.equal(out.faults[0].target, 'dev-channel');
    assert.deepEqual(double.appliedFaults, out.faults);
  });

  it('multiple faults are all applied', () => {
    const mod = fresh(FAULT_INJECTION);
    const double = makeTransportDouble();
    const faults = [
      { target: 'dev-channel', kind: 'drop', rate: 0.25 },
      { target: 'cdp', kind: 'delay', rate: 1 },
    ];
    const out = configureWithDouble(mod, { profile: { kind: 'highLatency', ms: 50 }, faults }, double);
    assert.equal(out.faults.length, 2);
    assert.equal(double.appliedFaults.length, 2);
  });

  it('empty faults list configures a clean session', () => {
    const mod = fresh(FAULT_INJECTION);
    const double = makeTransportDouble();
    const out = configureWithDouble(mod, { profile: { kind: 'highLatency', ms: 10 }, faults: [] }, double);
    assert.deepEqual(out.faults, []);
    assert.deepEqual(double.appliedFaults, []);
  });
});

describe('invalid combos reject (T9 / TC-DX-041)', () => {
  const cases = [
    ['unknown profile kind', { profile: { kind: 'turbo' }, faults: [] }],
    ['highLatency without ms', { profile: { kind: 'highLatency' }, faults: [] }],
    ['highLatency with negative ms', { profile: { kind: 'highLatency', ms: -5 }, faults: [] }],
    ['lossy without lossPct', { profile: { kind: 'lossy' }, faults: [] }],
    ['lossy with lossPct > 100', { profile: { kind: 'lossy', lossPct: 150 }, faults: [] }],
    ['fault rate > 1', { profile: { kind: 'highLatency', ms: 10 }, faults: [{ target: 'cdp', kind: 'drop', rate: 2 }] }],
    ['fault rate < 0', { profile: { kind: 'highLatency', ms: 10 }, faults: [{ target: 'cdp', kind: 'drop', rate: -0.1 }] }],
    ['fault missing target', { profile: { kind: 'highLatency', ms: 10 }, faults: [{ kind: 'drop', rate: 0.5 }] }],
    ['missing profile', { faults: [] }],
  ];
  for (const [name, opts] of cases) {
    it(`${name} rejects with {command:"configureSession", cause, action}`, () => {
      const mod = fresh(FAULT_INJECTION);
      const double = makeTransportDouble();
      assert.throws(
        () => configureWithDouble(mod, opts, double),
        (err) => {
          assert.equal(err.command, 'configureSession');
          assert.equal(typeof err.cause, 'string');
          assert.ok(err.cause.length > 0);
          assert.equal(typeof err.action, 'string');
          assert.ok(err.action.length > 0);
          return true;
        },
        `${name} must reject, never configure`,
      );
    });
  }
});
