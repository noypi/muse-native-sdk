'use strict';

class PkgError extends Error {
  constructor(code, path, expected, message) {
    super(message || `${code} at ${path}: expected ${expected}`);
    this.name = 'PkgError';
    this.code = code;
    this.path = path;
    this.expected = expected;
  }
}

module.exports = { PkgError };
