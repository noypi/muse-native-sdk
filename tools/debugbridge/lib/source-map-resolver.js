'use strict';
// TS→bytecode source-map resolver seam (FR-DX-009 / AC-DX-039).
// Wraps a resolver exposing `originalPositionFor(generated)` and maps
// Hermes bytecode frames back to their original TS sources. The default
// in-memory table is empty; tests inject a fake resolver double.

function createSourceMapResolver(entries) {
  const table = new Map(Object.entries(entries || {}));
  return {
    originalPositionFor(generated) {
      if (!generated || typeof generated.url !== 'string') {
        return { source: null, line: null, column: null };
      }
      const key = `${generated.url}:${generated.line}`;
      if (table.has(key)) return table.get(key);
      return { source: null, line: null, column: null };
    },
  };
}

function mapFrame(frame, resolver) {
  if (!frame || typeof frame !== 'object') return frame;
  if (!resolver || typeof resolver.originalPositionFor !== 'function') return frame;
  let mapped = null;
  try {
    mapped = resolver.originalPositionFor({
      url: frame.url,
      line: frame.lineNumber,
      column: frame.columnNumber,
    });
  } catch {
    return frame;
  }
  if (!mapped || !mapped.source) return frame;
  return {
    ...frame,
    source: mapped.source,
    originalLine: mapped.line,
    originalColumn: mapped.column,
  };
}

function mapCallFrames(frames, resolver) {
  if (!Array.isArray(frames)) return frames;
  return frames.map((frame) => mapFrame(frame, resolver));
}

module.exports = { createSourceMapResolver, mapFrame, mapCallFrames };
