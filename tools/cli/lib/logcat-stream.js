'use strict';
// T3 — FR-DX-008 / AC-DX-034 / TC-DX-034.
// Logcat streaming: classifies device lines via the reliability taxonomy
// label and symbolicates frames through the symbolication seam. Raw logcat
// text is never the primary output — every entry carries taxonomy + frame.

const seam = require('./symbolication-seam');

function toTaxonomy(level) {
  if (level === 'error' || level === 'fatal') return 'reliability.crash';
  if (typeof level === 'string' && level) return `log.${level}`;
  return 'log.info';
}

function matches(line, filter) {
  if (!filter || (filter.tag === undefined && filter.level === undefined)) return true;
  if (typeof line === 'string') {
    if (filter.tag && !line.includes(filter.tag)) return false;
    if (filter.level && !line.toLowerCase().includes(String(filter.level).toLowerCase())) return false;
    return true;
  }
  if (filter.tag !== undefined && line.tag !== filter.tag) return false;
  if (filter.level !== undefined && line.level !== filter.level) return false;
  return true;
}

function lineFrame(line) {
  if (typeof line === 'string') return line;
  return String(line.frame !== undefined ? line.frame : JSON.stringify(line));
}

function lineLevel(line) {
  if (typeof line === 'string') return 'info';
  return typeof line.level === 'string' ? line.level : 'info';
}

async function resolveLines(filter) {
  if (typeof seam.readLines !== 'function') {
    throw {
      command: 'logcat',
      cause: 'logcat transport unavailable: readLines missing',
      action: 'Check device connection via `adb devices`, then retry logcat streaming. [logcat AC-DX-034]',
    };
  }
  let lines;
  try {
    lines = await seam.readLines(filter);
  } catch (err) {
    throw {
      command: 'logcat',
      cause: err && err.message ? err.message : String(err),
      action: 'Check device connection via `adb devices`, then retry logcat streaming. [logcat AC-DX-034]',
    };
  }
  if (lines == null) return [];
  if (typeof lines[Symbol.asyncIterator] === 'function') {
    const out = [];
    for await (const line of lines) out.push(line);
    return out;
  }
  if (typeof lines[Symbol.iterator] === 'function' && typeof lines === 'object' && !Array.isArray(lines)) {
    return Array.from(lines);
  }
  if (Array.isArray(lines)) return lines;
  return [lines];
}

async function* streamLogcat(filter) {
  const active = filter && typeof filter === 'object' ? filter : {};
  let lines;
  try {
    lines = await resolveLines(active);
  } catch (err) {
    if (err && err.command === 'logcat') throw err;
    throw {
      command: 'logcat',
      cause: err && err.message ? err.message : String(err),
      action: 'Check device connection via `adb devices`, then retry logcat streaming. [logcat AC-DX-034]',
    };
  }
  for (const line of lines) {
    if (!matches(line, active)) continue;
    const frame = lineFrame(line);
    const taxonomy = toTaxonomy(lineLevel(line));
    let symbolicated;
    try {
      symbolicated = seam.symbolicate(frame);
      if (symbolicated && typeof symbolicated.then === 'function') {
        symbolicated = await symbolicated;
      }
    } catch (err) {
      throw {
        command: 'logcat',
        cause: err && err.message ? err.message : String(err),
        action: 'Retry logcat streaming; if symbolication keeps failing, rebuild symbols and retry. [logcat AC-DX-034]',
      };
    }
    const mappedSource =
      symbolicated && Object.prototype.hasOwnProperty.call(symbolicated, 'mappedSource')
        ? symbolicated.mappedSource
        : null;
    yield {
      taxonomy,
      frame: symbolicated && typeof symbolicated.frame === 'string' ? symbolicated.frame : frame,
      mappedSource: mappedSource === undefined ? null : mappedSource,
    };
  }
}

module.exports = { streamLogcat };
