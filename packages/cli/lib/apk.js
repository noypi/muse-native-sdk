'use strict';
// @muse/cli — unsigned unaligned APK assembler (FR-PACKAGING-004,
// AC-PACKAGING-008 / TC-PACKAGING-008).
// Pure module: `buildApk` takes fixture/real bytes in memory and returns
// unsigned unaligned ZIP bytes. Stored (method 0) entries only, so the
// output is deterministic and needs no compression dependency. Never
// touches the filesystem; signing/alignment is the T3 lane's job, so no
// META-INF/ entries are emitted here.

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) {
      c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    }
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  }
  return (c ^ 0xffffffff) >>> 0;
}

function dosDateTime(date) {
  return {
    time:
      ((date.getHours() & 0x1f) << 11) |
      ((date.getMinutes() & 0x3f) << 5) |
      (((date.getSeconds() / 2) | 0) & 0x1f),
    date:
      ((((date.getFullYear() - 1980) & 0x7f) << 9) |
        ((date.getMonth() + 1) << 5) |
        date.getDate()) >>>
      0,
  };
}

function fail(reason) {
  throw new Error(`apk: ${reason}`);
}

function checkBlob(value, label) {
  if (typeof value === 'string') {
    if (value.length === 0) fail(`${label} must not be empty`);
    return Buffer.from(value, 'utf8');
  }
  if (!Buffer.isBuffer(value) || value.length === 0) {
    fail(`${label} must be a non-empty Buffer`);
  }
  return value;
}

function checkLibName(part, label) {
  if (typeof part !== 'string' || part.length === 0) fail(`${label} must be a non-empty string`);
  if (part.trim().length === 0) fail(`${label} must not be empty or whitespace`);
  if (part.includes('/') || part.includes('\\') || part.includes('..')) {
    fail(`${label} must not contain path separators: ${part}`);
  }
  // NUL/control characters truncate or alias entry names on some
  // unzip/PackageManager implementations, collapsing two distinct names
  // into one on extraction. Fail closed before the entry is emitted.
  if (/[\0-\x1f\x7f]/.test(part)) {
    fail(`${label} must not contain control characters: ${part}`);
  }
}

function buildApk(input) {
  if (!input || typeof input !== 'object' || Array.isArray(input)) {
    fail('input must be a single object');
  }
  const manifestBytes = checkBlob(input.manifestXml, 'manifestXml');
  const dexBytes = checkBlob(input.dexBytes, 'dexBytes');
  const resourcesBytes = checkBlob(input.resourcesArsc, 'resourcesArsc');
  const treeBytes = checkBlob(input.treeBin, 'treeBin');
  if (!Array.isArray(input.nativeLibs)) fail('nativeLibs must be an array');

  const entries = [
    { name: 'AndroidManifest.xml', data: manifestBytes },
    { name: 'classes.dex', data: dexBytes },
    { name: 'resources.arsc', data: resourcesBytes },
  ];
  for (const lib of input.nativeLibs) {
    if (!lib || typeof lib !== 'object') fail('nativeLibs entries must be objects');
    checkLibName(lib.abi, 'nativeLib abi');
    checkLibName(lib.name, 'nativeLib name');
    const bytes = checkBlob(lib.bytes, `nativeLib bytes for ${lib.abi}`);
    entries.push({ name: `lib/${lib.abi}/${lib.name}`, data: bytes });
  }
  entries.push({ name: 'assets/tree.mui.bin', data: treeBytes });

  // Duplicate ZIP entry names alias on extraction (two entries collapsing
  // to one). Fail closed naming the duplicate before any bytes are emitted.
  const seenNames = new Set();
  for (const entry of entries) {
    if (seenNames.has(entry.name)) fail(`duplicate entry ${entry.name}`);
    seenNames.add(entry.name);
  }

  const { time, date } = dosDateTime(new Date());
  const chunks = [];
  const central = [];
  let offset = 0;
  for (const entry of entries) {
    const nameBuf = Buffer.from(entry.name, 'utf8');
    const crc = crc32(entry.data);
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(0, 6);
    local.writeUInt16LE(0, 8);
    local.writeUInt16LE(time, 10);
    local.writeUInt16LE(date, 12);
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(entry.data.length, 18);
    local.writeUInt32LE(entry.data.length, 22);
    local.writeUInt16LE(nameBuf.length, 26);
    local.writeUInt16LE(0, 28);
    chunks.push(local, nameBuf, entry.data);

    const header = Buffer.alloc(46);
    header.writeUInt32LE(0x02014b50, 0);
    header.writeUInt16LE(0x031e, 4);
    header.writeUInt16LE(20, 6);
    header.writeUInt16LE(0, 8);
    header.writeUInt16LE(0, 10);
    header.writeUInt16LE(time, 12);
    header.writeUInt16LE(date, 14);
    header.writeUInt32LE(crc, 16);
    header.writeUInt32LE(entry.data.length, 20);
    header.writeUInt32LE(entry.data.length, 24);
    header.writeUInt16LE(nameBuf.length, 28);
    header.writeUInt16LE(0, 30);
    header.writeUInt16LE(0, 32);
    header.writeUInt16LE(0, 34);
    header.writeUInt16LE(0, 36);
    header.writeUInt32LE(0, 38);
    header.writeUInt32LE(offset, 42);
    central.push(header, nameBuf);
    offset += local.length + nameBuf.length + entry.data.length;
  }

  const cdOffset = offset;
  const cdSize = central.reduce((sum, part) => sum + part.length, 0);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(0, 4);
  eocd.writeUInt16LE(0, 6);
  eocd.writeUInt16LE(entries.length, 8);
  eocd.writeUInt16LE(entries.length, 10);
  eocd.writeUInt32LE(cdSize, 12);
  eocd.writeUInt32LE(cdOffset, 16);
  eocd.writeUInt16LE(0, 20);

  return Buffer.concat([...chunks, ...central, eocd]);
}

module.exports = { buildApk };
