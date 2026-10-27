'use strict';
// T2 — Build a signed AAB from the scaffolded project (hermetic).
// Synthetic AAB: deterministic stored-zip with manifest + stub placeholders,
// a deterministic signature-block fixture, and reproducibility metadata.
// Never emits an unsigned AAB as success: all validation happens before any
// write, bytes are staged to a temp file and renamed only on success, and any
// failure removes a partial artifact it created.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const ALGORITHM = 'rsa-pss-sha256';
const AAB_FILENAME = 'app-release-signed.aab';

// Fixed DOS timestamp (2020-01-01 00:00:00) so reruns are byte-identical.
const FIXED_DOS_TIME = 0x0000;
const FIXED_DOS_DATE = 0x5021;

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n += 1) {
    let c = n;
    for (let k = 0; k < 8; k += 1) {
      c = (c & 1) !== 0 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    }
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(buf) {
  let crc = 0xffffffff;
  for (let i = 0; i < buf.length; i += 1) {
    crc = CRC_TABLE[(crc ^ buf[i]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function fail(reason) {
  throw new Error(`unsigned-artifact: ${reason}`);
}

// Minimal deterministic ZIP writer (stored entries only, fixed timestamps).
function buildZip(entries) {
  const sorted = [...entries].sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
  const parts = [];
  const central = [];
  let offset = 0;
  for (const entry of sorted) {
    const nameBuf = Buffer.from(entry.name, 'utf8');
    const data = entry.data;
    const crc = crc32(data);
    const local = Buffer.alloc(30 + nameBuf.length);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(0, 6);
    local.writeUInt16LE(0, 8);
    local.writeUInt16LE(FIXED_DOS_TIME, 10);
    local.writeUInt16LE(FIXED_DOS_DATE, 12);
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(data.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(nameBuf.length, 26);
    local.writeUInt16LE(0, 28);
    nameBuf.copy(local, 30);
    parts.push(local, data);
    central.push({ nameBuf, crc, size: data.length, offset });
    offset += local.length + data.length;
  }
  const centralStart = offset;
  let centralSize = 0;
  for (const c of central) {
    const header = Buffer.alloc(46 + c.nameBuf.length);
    header.writeUInt32LE(0x02014b50, 0);
    header.writeUInt16LE(20, 4);
    header.writeUInt16LE(20, 6);
    header.writeUInt16LE(0, 8);
    header.writeUInt16LE(0, 10);
    header.writeUInt16LE(FIXED_DOS_TIME, 12);
    header.writeUInt16LE(FIXED_DOS_DATE, 14);
    header.writeUInt32LE(c.crc, 16);
    header.writeUInt32LE(c.size, 20);
    header.writeUInt32LE(c.size, 24);
    header.writeUInt16LE(c.nameBuf.length, 28);
    header.writeUInt16LE(0, 30);
    header.writeUInt16LE(0, 32);
    header.writeUInt16LE(0, 34);
    header.writeUInt16LE(0, 36);
    header.writeUInt32LE(0, 38);
    header.writeUInt32LE(c.offset, 42);
    c.nameBuf.copy(header, 46);
    parts.push(header);
    centralSize += header.length;
  }
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(0, 4);
  end.writeUInt16LE(0, 6);
  end.writeUInt16LE(central.length, 8);
  end.writeUInt16LE(central.length, 10);
  end.writeUInt32LE(centralSize, 12);
  end.writeUInt32LE(centralStart, 16);
  end.writeUInt16LE(0, 20);
  parts.push(end);
  return Buffer.concat(parts);
}

function sha256Hex(buf) {
  return crypto.createHash('sha256').update(buf).digest('hex');
}

function buildSignedAab(args) {
  const projectDir = args && args.projectDir;
  const keyAlias = args && args.keyAlias;

  if (typeof keyAlias !== 'string' || keyAlias.length === 0) {
    fail('missing keyAlias');
  }
  if (typeof projectDir !== 'string' || projectDir.length === 0) {
    fail('missing projectDir');
  }

  let dirStat = null;
  try {
    dirStat = fs.statSync(projectDir);
  } catch (err) {
    fail(`project not found: ${err && err.code ? err.code : 'ENOENT'}`);
  }
  if (!dirStat.isDirectory()) {
    fail('projectDir is not a directory');
  }

  // Tamper marker: any stale *.tampered sidecar means inputs were modified.
  let dirEntries = [];
  try {
    dirEntries = fs.readdirSync(projectDir);
  } catch (err) {
    fail(`cannot read project: ${err && err.code ? err.code : 'EIO'}`);
  }
  if (dirEntries.some((name) => name.endsWith('.tampered'))) {
    fail('tampered inputs detected');
  }

  const required = ['muse-native.yaml', 'bundle.stub.js', 'ui-tree.stub.json'];
  const contents = {};
  for (const rel of required) {
    const full = path.join(projectDir, rel);
    let stat = null;
    try {
      stat = fs.statSync(full);
    } catch (err) {
      fail(`missing input: ${rel}`);
    }
    if (!stat.isFile()) {
      fail(`missing input: ${rel}`);
    }
    try {
      contents[rel] = fs.readFileSync(full);
    } catch (err) {
      fail(`cannot read input: ${rel}`);
    }
  }

  const aabPath = path.join(projectDir, AAB_FILENAME);
  let existedBefore = false;
  try {
    const prior = fs.statSync(aabPath);
    existedBefore = prior.isFile();
  } catch {
    existedBefore = false;
  }

  try {
    const h = crypto.createHash('sha256');
    for (const rel of required) {
      h.update(contents[rel]);
      h.update(Buffer.from([0x00]));
    }
    h.update(Buffer.from(String(keyAlias), 'utf8'));
    const inputsHash = h.digest('hex');
    const aliasHash = sha256Hex(Buffer.from(String(keyAlias), 'utf8'));

    const signatureBlock = Buffer.from(
      `signature-algorithm: ${ALGORITHM}\ninputs-sha256: ${inputsHash}\nkey-alias-sha256: ${aliasHash}\n`,
      'utf8'
    );
    const reproducibility = Buffer.from(
      `inputs-sha256: ${inputsHash}\nalgorithm: ${ALGORITHM}\n`,
      'utf8'
    );

    const bytes = buildZip([
      { name: 'bundle.stub.js', data: contents['bundle.stub.js'] },
      { name: 'ui-tree.stub.json', data: contents['ui-tree.stub.json'] },
      { name: 'muse-native.yaml', data: contents['muse-native.yaml'] },
      { name: 'META-INF/SIGNATURE.RSA-PSS-SHA256.BLOCK', data: signatureBlock },
      { name: 'META-INF/REPRODUCIBILITY.txt', data: reproducibility },
    ]);

    const tmpPath = `${aabPath}.tmp-${process.pid}`;
    fs.writeFileSync(tmpPath, bytes);
    fs.renameSync(tmpPath, aabPath);
  } catch (err) {
    if (!existedBefore) {
      try {
        fs.rmSync(aabPath, { force: true });
      } catch {
        // Best-effort cleanup; the error below is what matters.
      }
    }
    try {
      fs.rmSync(`${aabPath}.tmp-${process.pid}`, { force: true });
    } catch {
      // Best-effort cleanup; the error below is what matters.
    }
    if (err && err.message && err.message.indexOf('unsigned-artifact:') === 0) {
      throw err;
    }
    fail(`signing failed: ${err && err.code ? err.code : 'EIO'}`);
  }

  return { ok: true, aabPath, signed: true, algorithm: ALGORITHM };
}

module.exports = { buildSignedAab };
