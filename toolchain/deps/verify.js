'use strict';
// T3 — hash + ELF-machine + 16KB LOAD-alignment verification (fail closed).
// (FR-DEPS-009 AC-DEPS-024).
//
// Check order: hash -> ELF-machine-per-ABI -> 16KB LOAD alignment. Every
// failure throws a SetupError naming artifact, ABI, and check. Synchronous;
// makes zero network calls.

const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const SUPPORTED_ABIS = ['arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64'];

// Expected ELF machine per ABI (A4).
const ABI_MACHINE = {
  'arm64-v8a': 183, // EM_AARCH64
  'armeabi-v7a': 40, // EM_ARM
  x86: 3, // EM_386
  x86_64: 62, // EM_X86_64
};

const PAGE_16K = 16384;

class SetupError extends Error {
  constructor(code, artifact, abi, check) {
    super(`${String(code)}: artifact '${String(artifact)}' for ABI '${String(abi)}' failed check '${String(check)}'`);
    this.name = 'SetupError';
    this.code = code;
    this.artifact = artifact;
    this.abi = abi;
    if (check !== undefined) this.check = check;
  }
}

function artifactOf(p) {
  try {
    if (typeof p !== 'string' || p.length === 0) return 'unknown';
    const base = path.basename(p);
    return base.length > 0 ? base : 'unknown';
  } catch {
    return 'unknown';
  }
}

// Strip common archive/library suffixes so a fetched file basename can be
// matched against its T1 pin artifact id (e.g. "lib.so", ".tar.gz").
function pinCandidates(basename) {
  const out = [basename];
  const suffixes = ['.tar.gz', '.tgz', '.tar', '.zip', '.so', '.node'];
  for (const s of suffixes) {
    if (basename.endsWith(s) && basename.length > s.length) {
      out.push(basename.slice(0, -s.length));
    }
  }
  return out;
}

function loadPinsMap() {
  try {
    const raw = fs.readFileSync(path.join(__dirname, 'pins.json'), 'utf8');
    const data = JSON.parse(raw);
    if (data === null || typeof data !== 'object' || Array.isArray(data)) return null;
    return data;
  } catch {
    return null;
  }
}

function expectedHashFor(artifact, abi) {
  const pins = loadPinsMap();
  if (!pins) return undefined;
  const entries = pins[abi];
  if (!Array.isArray(entries)) return undefined;
  const names = pinCandidates(artifact);
  for (const entry of entries) {
    if (entry === null || typeof entry !== 'object' || Array.isArray(entry)) continue;
    const id = typeof entry.artifact === 'string' ? entry.artifact : entry.name;
    if (typeof id !== 'string') continue;
    if (!names.includes(id)) continue;
    if (typeof entry.sha256 === 'string' && /^[0-9a-fA-F]{64}$/.test(entry.sha256)) {
      return entry.sha256.toLowerCase();
    }
    return undefined;
  }
  return undefined;
}

function readU16(buf, off, le) {
  return le ? buf.readUInt16LE(off) : buf.readUInt16BE(off);
}

function readU32(buf, off, le) {
  return le ? buf.readUInt32LE(off) : buf.readUInt32BE(off);
}

function readU64(buf, off, le) {
  const v = le ? buf.readBigUInt64LE(off) : buf.readBigUInt64BE(off);
  return v;
}

// Parse ELF header + program headers. Throws SetupError(EELF) on malformed
// headers. Returns { machine, loads: [{ pAlign, pVaddr }] }.
function parseElf(buf, artifact, abi) {
  const fail = () => {
    throw new SetupError('EELF', artifact, abi, 'elf-machine');
  };
  if (!Buffer.isBuffer(buf) || buf.length < 52) fail();
  if (buf[0] !== 0x7f || buf[1] !== 0x45 || buf[2] !== 0x4c || buf[3] !== 0x46) fail();
  const eiClass = buf[4]; // 1 = 32-bit, 2 = 64-bit
  const eiData = buf[5]; // 1 = LE, 2 = BE
  if ((eiClass !== 1 && eiClass !== 2) || (eiData !== 1 && eiData !== 2)) fail();
  const le = eiData === 1;
  const is64 = eiClass === 2;
  const machine = readU16(buf, 18, le);
  const ehsize = readU16(buf, is64 ? 52 : 40, le);
  const phentsize = readU16(buf, is64 ? 54 : 42, le);
  const phnum = readU16(buf, is64 ? 56 : 44, le);
  let phoff;
  try {
    phoff = Number(readU64(buf, is64 ? 32 : 28, le));
  } catch {
    fail();
  }
  if (!Number.isSafeInteger(phoff) || phoff < 0) fail();
  const needEnt = is64 ? 56 : 32;
  if (ehsize < (is64 ? 64 : 52) || phentsize < needEnt || phnum < 1) fail();
  if (phoff + phnum * phentsize > buf.length) fail();
  const loads = [];
  for (let i = 0; i < phnum; i += 1) {
    const o = phoff + i * phentsize;
    const pType = readU32(buf, o, le);
    if (pType !== 1) continue; // PT_LOAD only
    let pAlign;
    let pVaddr;
    try {
      if (is64) {
        pVaddr = readU64(buf, o + 16, le);
        pAlign = readU64(buf, o + 48, le);
      } else {
        pVaddr = BigInt(readU32(buf, o + 8, le));
        pAlign = BigInt(readU32(buf, o + 28, le));
      }
    } catch {
      fail();
    }
    loads.push({ pAlign, pVaddr });
  }
  return { machine, loads };
}

function verifyArtifact(filePath, abi) {
  const artifact = artifactOf(filePath);
  // Hash gate (first): path must be usable, file must exist, and its sha256
  // must match the T1 pin for (artifact, abi). Anything else fails closed
  // with EHASH before ELF is consulted.
  if (typeof filePath !== 'string' || filePath.length === 0) {
    throw new SetupError('EHASH', artifact, abi, 'hash');
  }
  let bytes;
  try {
    bytes = fs.readFileSync(filePath);
  } catch {
    throw new SetupError('EHASH', artifact, abi, 'hash');
  }
  let actual = '';
  try {
    actual = crypto.createHash('sha256').update(bytes).digest('hex');
  } catch {
    throw new SetupError('EHASH', artifact, abi, 'hash');
  }
  const expected = expectedHashFor(artifact, abi);
  if (!expected || actual.toLowerCase() !== expected) {
    throw new SetupError('EHASH', artifact, abi, 'hash');
  }

  // ELF-machine-per-ABI gate.
  const elf = parseElf(bytes, artifact, abi);
  const want = ABI_MACHINE[abi];
  if (want === undefined || elf.machine !== want) {
    throw new SetupError('EELF', artifact, abi, 'elf-machine');
  }

  // 16KB LOAD-alignment gate: every PT_LOAD segment must carry a 16KB
  // multiple p_align and a 16KB-aligned p_vaddr.
  if (elf.loads.length === 0) {
    throw new SetupError('EALIGN', artifact, abi, 'load-alignment');
  }
  for (const seg of elf.loads) {
    const align = seg.pAlign;
    const vaddr = seg.pVaddr;
    const alignOk = align !== 0n && align % BigInt(PAGE_16K) === 0n;
    const vaddrOk = vaddr % BigInt(PAGE_16K) === 0n;
    if (!alignOk || !vaddrOk) {
      throw new SetupError('EALIGN', artifact, abi, 'load-alignment');
    }
  }

  return { ok: true };
}

module.exports = { verifyArtifact, SetupError };
