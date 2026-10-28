'use strict';

function showErrorOverlay(error, previousUI) {
  const frame = (error && error.sourceMap) || (error && error.frame) || {};
  const location =
    frame.file != null && frame.line != null
      ? `${frame.file}:${frame.line}`
      : '';
  const message = (error && error.message) || 'Unknown error';
  const text = location ? `${message} at ${location}` : `${message}`;

  const handle = {
    text,
    previousUI,
    dismissed: false,
    dismiss() {
      handle.dismissed = true;
      return previousUI;
    },
  };

  return handle;
}

module.exports = { showErrorOverlay };
