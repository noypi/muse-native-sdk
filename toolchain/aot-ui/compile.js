'use strict';
// T5 — deterministic AOT-UI compiler core (NFR-BUILD-013 / AC-BUILD-041, AC-BUILD-045).
// Hermetic: stdlib only (node:crypto). No timestamps, no absolute paths in output;
// stable key ordering for byte-identical deterministic emission.

const { createHash } = require('node:crypto');

const DEFAULT_FILENAME = 'unknown.tsx';
const PAYLOAD_VERSION = 1;

class AotError extends Error {
  constructor({ file, line, column, elementPath, message }) {
    super(message);
    this.name = 'AotError';
    this.file = file;
    this.line = line;
    this.column = column;
    this.elementPath = elementPath;
  }
}

function lineColumnOf(source, offset) {
  const clamped = Math.max(0, Math.min(offset, source.length));
  let line = 1;
  let lineStart = 0;
  for (let i = 0; i < clamped; i++) {
    if (source[i] === '\n') {
      line += 1;
      lineStart = i + 1;
    }
  }
  return { line, column: clamped - lineStart + 1 };
}

function elementPathOf(stack) {
  // stack[0] is the synthetic root; join open element names.
  const names = [];
  for (let i = 1; i < stack.length; i++) {
    names.push(stack[i].name);
  }
  return names.length > 0 ? names.join('/') : '(root)';
}

function fail(source, file, offset, elementPath, message) {
  const { line, column } = lineColumnOf(source, offset);
  throw new AotError({ file, line, column, elementPath, message });
}

function isNameChar(ch) {
  return ch === '_' || ch === '$' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch === '.' || ch === '-';
}

function skipWhitespace(source, i) {
  while (i < source.length) {
    const ch = source[i];
    if (ch === ' ' || ch === '\t' || ch === '\n' || ch === '\r' || ch === '\f' || ch === '\v') {
      i += 1;
    } else {
      break;
    }
  }
  return i;
}

function readName(source, i) {
  const start = i;
  if (i < source.length && /[A-Za-z_$]/.test(source[i])) {
    i += 1;
    while (i < source.length && isNameChar(source[i])) {
      i += 1;
    }
    return { name: source.slice(start, i), next: i };
  }
  return null;
}

// Scan a balanced { ... } expression starting at `i` (source[i] === '{').
// Returns { value, next } where next is the offset after the closing brace,
// or null when unterminated.
function readBraceExpression(source, i) {
  let depth = 0;
  let quote = null; // "'", '"', or '`'
  let escaped = false;
  let j = i;
  while (j < source.length) {
    const ch = source[j];
    if (quote !== null) {
      if (escaped) {
        escaped = false;
      } else if (ch === '\\') {
        escaped = true;
      } else if (ch === quote) {
        quote = null;
      }
      j += 1;
      continue;
    }
    if (ch === '"' || ch === "'" || ch === '`') {
      quote = ch;
      j += 1;
      continue;
    }
    if (ch === '{') {
      depth += 1;
    } else if (ch === '}') {
      depth -= 1;
      if (depth === 0) {
        return { value: source.slice(i + 1, j), next: j + 1 };
      }
    }
    j += 1;
  }
  return null;
}

function readQuoted(source, i) {
  const quote = source[i];
  let j = i + 1;
  let out = '';
  while (j < source.length) {
    const ch = source[j];
    if (ch === '\\' && j + 1 < source.length) {
      out += ch + source[j + 1];
      j += 2;
      continue;
    }
    if (ch === quote) {
      return { value: out, next: j + 1 };
    }
    if (ch === '\n') {
      break; // quoted attribute values stay on one line; newline means unterminated
    }
    out += ch;
    j += 1;
  }
  return null;
}

function setProp(props, key, value) {
  // Null-prototype holder + defineProperty: "__proto__" stays a plain key.
  Object.defineProperty(props, key, {
    value,
    writable: true,
    enumerable: true,
    configurable: true,
  });
}

function parseElementTree(source, file) {
  const root = { name: '#root', children: [] };
  const stack = [root];

  let i = 0;
  const len = source.length;

  function openPath() {
    return elementPathOf(stack);
  }

  while (i < len) {
    const ch = source[i];
    if (ch === '<') {
      if (source.startsWith('<!--', i)) {
        const end = source.indexOf('-->', i + 4);
        if (end === -1) {
          fail(source, file, i, openPath(), `Unterminated comment at ${JSON.stringify(openPath())}`);
        }
        i = end + 3;
        continue;
      }
      if (source[i + 1] === '/') {
        // Closing tag: </Name> or </> (fragment close).
        let j = skipWhitespace(source, i + 2);
        let name = null;
        if (source[j] === '>') {
          name = '';
          j += 1;
        } else {
          const parsed = readName(source, j);
          if (parsed === null) {
            fail(source, file, i, openPath(), `Malformed closing tag near ${JSON.stringify(source.slice(i, i + 16))}`);
          }
          name = parsed.name;
          j = skipWhitespace(source, parsed.next);
          if (source[j] !== '>') {
            fail(source, file, i, openPath(), `Malformed closing tag for element ${JSON.stringify(name)}`);
          }
          j += 1;
        }
        if (stack.length <= 1) {
          fail(source, file, i, '(root)', `Unexpected closing tag ${JSON.stringify(name || 'fragment')} with no open element`);
        }
        const open = stack[stack.length - 1];
        const want = open.name;
        const gotFragment = name === '';
        const wantFragment = want === '';
        const match = gotFragment && wantFragment ? true : name === want;
        if (!match) {
          fail(
            source,
            file,
            i,
            openPath(),
            `Mismatched closing tag: expected ${JSON.stringify(wantFragment ? 'fragment' : want)} but found ${JSON.stringify(gotFragment ? 'fragment' : name)} while ${JSON.stringify(openPath())} is open`
          );
        }
        stack.pop();
        const node = {
          kind: 'element',
          name: open.name,
          props: open.props,
          children: open.children,
        };
        stack[stack.length - 1].children.push(node);
        i = j;
        continue;
      }
      // Opening tag, self-closing tag, or fragment open <>.
      if (source[i + 1] === '>') {
        stack.push({ name: '', props: Object.create(null), children: [], offset: i });
        i += 2;
        continue;
      }
      const parsed = readName(source, skipWhitespace(source, i + 1));
      if (parsed === null) {
        fail(source, file, i, openPath(), `Malformed opening tag near ${JSON.stringify(source.slice(i, i + 16))}`);
      }
      const name = parsed.name;
      let j = skipWhitespace(source, parsed.next);
      const props = Object.create(null);
      let closed = false;
      let selfClosed = false;
      while (j < len) {
        if (source.startsWith('/>', j)) {
          j += 2;
          closed = true;
          selfClosed = true;
          break;
        }
        if (source[j] === '>') {
          j += 1;
          closed = true;
          break;
        }
        if (source[j] === '<' || source[j] === '{' || source[j] === '}') {
          fail(source, file, j, stack.length > 1 ? openPath() + '/' + name : name, `Malformed attribute in element ${JSON.stringify(name)}`);
        }
        const attr = readName(source, j);
        if (attr === null) {
          fail(source, file, j, stack.length > 1 ? openPath() + '/' + name : name, `Malformed attribute in element ${JSON.stringify(name)}`);
        }
        j = skipWhitespace(source, attr.next);
        let value = true;
        if (source[j] === '=') {
          j = skipWhitespace(source, j + 1);
          if (source[j] === '"' || source[j] === "'") {
            const q = readQuoted(source, j);
            if (q === null) {
              fail(source, file, j, openPath(), `Unterminated attribute value in element ${JSON.stringify(name)}`);
            }
            value = q.value;
            j = q.next;
          } else if (source[j] === '{') {
            const expr = readBraceExpression(source, j);
            if (expr === null) {
              fail(source, file, j, openPath(), `Unterminated expression attribute in element ${JSON.stringify(name)}`);
            }
            value = { expr: expr.value };
            j = expr.next;
          } else {
            const start = j;
            while (j < len && !/[\s<>"'{}>/]/.test(source[j])) {
              j += 1;
            }
            if (j === start) {
              fail(source, file, j, openPath(), `Malformed attribute value in element ${JSON.stringify(name)}`);
            }
            value = source.slice(start, j);
          }
        }
        setProp(props, attr.name, value);
        j = skipWhitespace(source, j);
      }
      if (!closed) {
        fail(source, file, i, openPath(), `Unterminated opening tag for element ${JSON.stringify(name)}`);
      }
      if (selfClosed) {
        stack[stack.length - 1].children.push({ kind: 'element', name, props, children: [] });
      } else {
        stack.push({ name, props, children: [], offset: i });
      }
      i = j;
      continue;
    }
    if (ch === '{') {
      const expr = readBraceExpression(source, i);
      if (expr === null) {
        fail(source, file, i, openPath(), `Unterminated expression in ${JSON.stringify(openPath())}`);
      }
      stack[stack.length - 1].children.push({ kind: 'expr', value: expr.value });
      i = expr.next;
      continue;
    }
    if (ch === '}') {
      fail(source, file, i, openPath(), `Unexpected character ${JSON.stringify('}')} in ${JSON.stringify(openPath())}`);
    }
    // Text span until the next '<' or '{' or '}'.
    let j = i;
    while (j < len && source[j] !== '<' && source[j] !== '{' && source[j] !== '}') {
      j += 1;
    }
    const text = source.slice(i, j);
    if (text.length > 0) {
      stack[stack.length - 1].children.push({ kind: 'text', value: text });
    }
    i = j;
  }

  if (stack.length > 1) {
    const open = stack[stack.length - 1];
    fail(
      source,
      file,
      len,
      openPath(),
      `Unclosed element ${JSON.stringify(open.name === '' ? 'fragment' : open.name)}: reached end of input with ${JSON.stringify(openPath())} still open`
    );
  }

  return root.children;
}

function stableStringify(value) {
  if (value === null || value === undefined) {
    return 'null';
  }
  const t = typeof value;
  if (t === 'string') {
    return JSON.stringify(value);
  }
  if (t === 'number') {
    if (!Number.isFinite(value)) {
      return 'null';
    }
    return JSON.stringify(value);
  }
  if (t === 'boolean') {
    return value ? 'true' : 'false';
  }
  if (Array.isArray(value)) {
    let out = '[';
    for (let k = 0; k < value.length; k++) {
      if (k > 0) {
        out += ',';
      }
      out += stableStringify(value[k]);
    }
    return out + ']';
  }
  if (t === 'object') {
    const keys = Object.keys(value).sort();
    let out = '{';
    let first = true;
    for (const key of keys) {
      if (!first) {
        out += ',';
      }
      first = false;
      out += JSON.stringify(key) + ':' + stableStringify(value[key]);
    }
    return out + '}';
  }
  return 'null';
}

function compile(source, opts) {
  if (typeof source !== 'string') {
    throw new TypeError('compile: source must be a string');
  }
  let file = DEFAULT_FILENAME;
  if (opts !== undefined && opts !== null) {
    if (typeof opts !== 'object') {
      throw new TypeError('compile: opts must be an object when provided');
    }
    if (opts.filename !== undefined && opts.filename !== null) {
      if (typeof opts.filename !== 'string' || opts.filename.length === 0) {
        throw new TypeError('compile: opts.filename must be a non-empty string when provided');
      }
      file = opts.filename;
    }
  }

  // Parse first so syntax errors throw before any emission.
  const children = parseElementTree(source, file);

  // Deterministic payload: fixed version + tree only. Never embeds the
  // filename, timestamps, or absolute paths; object keys sort on emission.
  const payload = { version: PAYLOAD_VERSION, root: children };
  const text = stableStringify(payload);
  const bytes = Buffer.from(text, 'utf8');
  const hash = createHash('sha256').update(bytes).digest('hex');
  return { bytes, hash };
}

module.exports = { compile, AotError, DEFAULT_FILENAME };
