'use strict';
// @muse/cli — single combined build log for both validators (FR-TSX-009).
// Collect-then-exit: the tsc verdict and the AOT-legality verdict are both
// collected into ONE log string, each line naming its validator and source
// location. Either failure exits non-zero, but the other validator always
// still runs to completion (never throw-early).
//
// Pure string building only — no I/O here. The caller in `lib/build.js`
// owns any file writing. Both functions defensively coerce weird input and
// never throw.

// One appended log line naming the validator + `file:line:column`.
// Never throws: non-string logs and partial entries are coerced.
function appendVerdict(log, entry) {
  const base = typeof log === 'string' ? log : log == null ? '' : String(log);
  const source =
    entry && typeof entry === 'object' && !Array.isArray(entry)
      ? entry
      : { message: entry == null ? '' : String(entry) };
  const validator =
    source.validator == null || source.validator === ''
      ? 'unknown'
      : String(source.validator);
  const file = source.file == null || source.file === '' ? 'unknown' : String(source.file);
  const lineNumber = Number(source.line);
  const line = Number.isFinite(lineNumber) ? Math.trunc(lineNumber) : 0;
  const columnNumber = Number(source.column);
  const column = Number.isFinite(columnNumber) ? Math.trunc(columnNumber) : 0;
  const nodePath = source.nodePath == null ? '' : String(source.nodePath);
  const message = source.message == null ? '' : String(source.message);
  const location = `${file}:${line}:${column}`;
  const tail = `${nodePath ? ` ${nodePath}` : ''}${message ? ` ${message}` : ''}`;
  const prefix = base === '' ? '' : base.endsWith('\n') ? '' : '\n';
  return `${base}${prefix}[${validator}] ${location}${tail}\n`;
}

// Normalize one raw diagnostic from a validator fn into an entry object.
// Missing validators default to the fn that produced them so the combined
// log always names a validator. Never throws.
function toEntry(raw, fallbackValidator) {
  if (raw && typeof raw === 'object' && !Array.isArray(raw)) {
    if (raw.validator == null || raw.validator === '') {
      const normalized = Object.assign({}, raw);
      normalized.validator = fallbackValidator;
      return normalized;
    }
    return raw;
  }
  return { validator: fallbackValidator, message: raw == null ? '' : String(raw) };
}

// Collect one validator fn's diagnostics without ever throwing: a throwing
// validator contributes a single crash diagnostic naming that validator so
// the OTHER validator still runs to completion and the combined log records
// the crash. Never throws; never lets a validator throw escape.
function collectDiagnostics(fn, fallbackValidator) {
  const fallback =
    fallbackValidator == null || fallbackValidator === ''
      ? 'unknown'
      : String(fallbackValidator);
  if (typeof fn !== 'function') return [];
  let result;
  try {
    result = fn();
  } catch (err) {
    const message =
      err instanceof Error
        ? err.message
        : err != null && typeof err === 'object' && 'message' in err
          ? String(err.message)
          : String(err);
    return [
      {
        validator: fallback,
        file: '',
        line: 0,
        column: 0,
        nodePath: '',
        message,
      },
    ];
  }
  return Array.isArray(result) ? result : [];
}

// Run BOTH validators always; any diagnostic yields `exitCode: 1` with one
// combined log line per diagnostic, clean yields `exitCode: 0`. Never throws.
function runBothValidators(tscFn, aotFn) {
  const tscDiags = collectDiagnostics(tscFn, 'tsc');
  const aotDiags = collectDiagnostics(aotFn, 'aot');
  let log = '';
  for (const raw of tscDiags) {
    log = appendVerdict(log, toEntry(raw, 'tsc'));
  }
  for (const raw of aotDiags) {
    log = appendVerdict(log, toEntry(raw, 'aot'));
  }
  const exitCode = tscDiags.length + aotDiags.length > 0 ? 1 : 0;
  return { log, exitCode };
}

module.exports = { appendVerdict, collectDiagnostics, runBothValidators };
