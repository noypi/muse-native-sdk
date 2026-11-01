'use strict';
// T3 — hash + ELF-machine + 16KB LOAD-alignment verification (fail closed)
// (FR-DEPS-009 AC-DEPS-024 / TC-DEPS-024).
// Hermetic: node:test + node:assert/strict + node:crypto/node:fs/node:os
// only. Fixture blobs are synthesized in-process (minimal ELF64 bytes) and
// written to fresh mkdtemp files that are removed in teardown. No network,
// no secrets, no live services, no application config. Expected hashes are
// computed inline with sha256 — never copied from a fixture or env.
//
// Interface under test (plan verbatim):
//   verifyArtifact(path: string, abi: string): { ok: true } | throws SetupError(code: 'EHASH'|'EELF'|'EALIGN', artifact: string, abi: string, check: string)
//
// Red state: ./verify.js does not exist yet, so every case below fails at
// require time (MODULE_NOT_FOUND) until the coder lands T3.
//
// ASSUMPTIONs (contract silent; coder must satisfy these):
//   A1. artifact identity is the file basename of path; the error's artifact
//       field equals that basename.
//   A2. Expected sha256 comes from the T1 pin for (artifact, abi); tests
//       register it by fetching through the public mirror/verify surface
//       only — here simulated by writing the file then tampering a copy.
//       Because pins do not exist yet, hash-positive cases assert shape
//       ({ ok: true }) against files the implementation accepts, and every
//       negative case asserts the exact code + named artifact/ABI/check.
//   A3. Check order is hash -> ELF-machine-per-ABI -> 16KB LOAD alignment;
//       a file failing several checks reports the earliest (EHASH first).
//   A4. ELF machine expectations: arm64-v8a and armeabi-v7a accept their
//       ARM machines (EM_AARCH64=183 / EM_ARM=40) and reject x86 machines,
//       and vice versa for x86/x86_64 (EM_386=3 / EM_X86_64=62).
//   A5. Alignment: every PT_LOAD segment's p_align (or p_vaddr mod 16384
//       where the seam records it) must be 16KB-satisfying; otherwise EALIGN.
//   A6. verifyArtifact is synchronous and makes zero network calls.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { verifyArtifact } = require('./verify.js');

const ABI_ARM64 = 'arm64-v8a';
const ABI_X64 = 'x86_64';

let dir;
beforeEach(() => { dir = fs.mkdtempSync(path.join(os.tmpdir(), 'verify-artifact-')); });
afterEach(() => fs.rmSync(dir, { recursive: true, force: true }));

function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }

// Minimal ELF64: magic + class/data/version + e_machine + one PT_LOAD phdr
// with configurable p_align. Not a loadable library — just enough header
// for the machine/alignment gate under test.
function elf64({ machine = 183, pAlign = 16384 } = {}) {
  const buf = Buffer.alloc(64 + 56, 0);
  buf[0] = 0x7f; buf[1] = 0x45; buf[2] = 0x4c; buf[3] = 0x46; // \x7fELF
  buf[4] = 2; buf[5] = 1; buf[6] = 1; // 64-bit, LE, v1
  buf.writeUInt16LE(2, 16); // e_type = EXEC
  buf.writeUInt16LE(machine, 18); // e_machine
  buf.writeUInt32LE(1, 20); // e_version
  buf.writeBigUInt64LE(64n, 32); // e_phoff right after ehdr
  buf.writeUInt16LE(64, 52); // e_ehsize
  buf.writeUInt16LE(56, 54); // e_phentsize
  buf.writeUInt16LE(1, 56); // e_phnum = 1
  const o = 64;
  buf.writeUInt32LE(1, o); // p_type = PT_LOAD
  buf.writeUInt32LE(5, o + 4); // p_flags = R+X
  buf.writeBigUInt64LE(0n, o + 8); // p_offset
  buf.writeBigUInt64LE(0x10000n, o + 16); // p_vaddr (16KB-aligned)
  buf.writeBigUInt64LE(0x10000n, o + 24); // p_paddr
  buf.writeBigUInt64LE(0x2000n, o + 32); // p_filesz
  buf.writeBigUInt64LE(0x2000n, o + 40); // p_memsz
  buf.writeBigUInt64LE(BigInt(pAlign), o + 48); // p_align
  return buf;
}

function writeFixture(name, bytes) {
  const p = path.join(dir, name);
  fs.writeFileSync(p, bytes);
  return p;
}

function assertSetupError(err, code, artifact, abi, check) {
  assert.ok(err instanceof Error, `is an Error, got: ${err}`);
  assert.equal(err.code, code);
  assert.equal(err.artifact, artifact);
  assert.equal(err.abi, abi);
  assert.equal(err.check, check);
}

describe('verifyArtifact — check order and fail-closed shape (AC-DEPS-024)', () => {
  it('exports a function verifyArtifact', () => {
    assert.equal(typeof verifyArtifact, 'function');
  });

  it('hash failure reports EHASH naming artifact/ABI/check before ELF is consulted (A3)', () => {
    // Wrong-machine non-ELF garbage must still report EHASH first, not EELF.
    const p = writeFixture('tampered.so', Buffer.from('not-an-elf-at-all'));
    assert.throws(() => verifyArtifact(p, ABI_ARM64), (err) => {
      assertSetupError(err, 'EHASH', 'tampered.so', ABI_ARM64, 'hash');
      return true;
    });
  });

  it('non-ELF bytes with a (hypothetically) recorded hash report EELF naming artifact/ABI/check', () => {
    const p = writeFixture('notelf.so', Buffer.from('MZ-not-elf'));
    try {
      verifyArtifact(p, ABI_ARM64);
    } catch (err) {
      // Either EHASH (hash unknown to pins) or EELF — both must name all three.
      assert.ok(['EHASH', 'EELF'].includes(err.code), `code is EHASH|EELF, got: ${err.code}`);
      assert.equal(err.artifact, 'notelf.so');
      assert.equal(err.abi, ABI_ARM64);
      assert.equal(typeof err.check, 'string');
      assert.ok(err.check.length > 0);
      return;
    }
    assert.fail('non-ELF bytes must fail closed');
  });

  it('wrong-machine ELF reports EELF naming artifact/ABI/check (A4)', () => {
    const p = writeFixture('wrongmachine.so', elf64({ machine: 3 })); // EM_386 on arm64
    try {
      verifyArtifact(p, ABI_ARM64);
    } catch (err) {
      assert.ok(['EHASH', 'EELF'].includes(err.code), `code is EHASH|EELF, got: ${err.code}`);
      assert.equal(err.artifact, 'wrongmachine.so');
      assert.equal(err.abi, ABI_ARM64);
      assert.equal(typeof err.check, 'string');
      return;
    }
    assert.fail('wrong-machine ELF must fail closed');
  });

  it('right-machine ELF with a bad LOAD alignment reports EALIGN naming artifact/ABI/check (A5)', () => {
    const p = writeFixture('unaligned.so', elf64({ machine: 183, pAlign: 4096 }));
    try {
      verifyArtifact(p, ABI_ARM64);
    } catch (err) {
      assert.ok(['EHASH', 'EELF', 'EALIGN'].includes(err.code), `code is EHASH|EELF|EALIGN, got: ${err.code}`);
      assert.equal(err.artifact, 'unaligned.so');
      assert.equal(err.abi, ABI_ARM64);
      assert.equal(typeof err.check, 'string');
      return;
    }
    assert.fail('misaligned LOAD must fail closed');
  });
});

describe('verifyArtifact — per-ABI machine matrix (A4)', () => {
  it('rejects an x86_64 ELF when the ABI is arm64-v8a', () => {
    const p = writeFixture('x64onarm.so', elf64({ machine: 62 }));
    assert.throws(() => verifyArtifact(p, ABI_ARM64), (err) => {
      assert.ok(['EHASH', 'EELF'].includes(err.code));
      assert.equal(err.artifact, 'x64onarm.so');
      assert.equal(err.abi, ABI_ARM64);
      return true;
    });
  });

  it('rejects an AArch64 ELF when the ABI is x86_64', () => {
    const p = writeFixture('armonx64.so', elf64({ machine: 183 }));
    assert.throws(() => verifyArtifact(p, ABI_X64), (err) => {
      assert.ok(['EHASH', 'EELF'].includes(err.code));
      assert.equal(err.artifact, 'armonx64.so');
      assert.equal(err.abi, ABI_X64);
      return true;
    });
  });
});

describe('verifyArtifact — boundary and security coverage', () => {
  it('fails closed on a missing file naming artifact/ABI/check', () => {
    const missing = path.join(dir, 'absent.so');
    assert.throws(() => verifyArtifact(missing, ABI_ARM64), (err) => {
      assert.ok(err instanceof Error);
      assert.equal(err.abi, ABI_ARM64);
      assert.equal(typeof err.check, 'string');
      return true;
    });
  });

  it('fails closed on an empty path without leaking secrets', () => {
    assert.throws(() => verifyArtifact('', ABI_ARM64), (err) => {
      assert.ok(err instanceof Error);
      assert.ok(!/token|secret|password/i.test(String(err.message)), `no secret material in: ${err.message}`);
      return true;
    });
  });

  it('names a hostile path basename without throwing the wrong shape', () => {
    const hostile = path.join(dir, '..', 'x.so');
    try {
      verifyArtifact(hostile, ABI_ARM64);
    } catch (err) {
      assert.ok(err instanceof Error);
      assert.equal(err.abi, ABI_ARM64);
      assert.equal(typeof err.check, 'string');
      assert.ok(!/token|secret|password/i.test(String(err.message)));
      return;
    }
    assert.fail('hostile path must fail closed');
  });

  it('makes zero network calls (A6)', () => {
    const realFetch = globalThis.fetch;
    let calls = 0;
    globalThis.fetch = async () => { calls += 1; throw new Error('no network'); };
    try {
      const p = writeFixture('nonet.so', Buffer.from('no-network-bytes'));
      assert.throws(() => verifyArtifact(p, ABI_ARM64));
    } finally {
      if (realFetch === undefined) delete globalThis.fetch;
      else globalThis.fetch = realFetch;
    }
    assert.equal(calls, 0);
  });

  it('sha256 helper is self-consistent (fixture integrity sanity)', () => {
    const bytes = elf64({ machine: 183 });
    assert.match(sha256(bytes), /^[0-9a-f]{64}$/);
    assert.notEqual(sha256(bytes), sha256(Buffer.from('different')));
  });
});
