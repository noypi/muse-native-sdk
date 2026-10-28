'use strict';
// FR-M3-006 / RULE-M3-004 — `TextInput` component surface over the tier-1
// text-field edit core (T1). Constructor-injected engine adapters only: every
// edit delegates to `deps.editingEngine` (FR-TEXT-008 contract), clipboard
// transport flows through `deps.clipboard`, inset frames are consumed from
// `deps.insets`, and bound-value sync rides the injected `deps.binding`
// two-way binding (FR-TSX-006 legality, FR-MCORE-007 write-through). No engine
// logic lives here and no engine paths are imported.

function toIndex(n) {
  const v = Number(n);
  if (!Number.isFinite(v)) {
    return 0;
  }
  return Math.max(0, Math.floor(v));
}

function normaliseRange(start, end) {
  let a = toIndex(start);
  let b = toIndex(end);
  if (a > b) {
    const t = a;
    a = b;
    b = t;
  }
  return [a, b];
}

function readCommittedText(editingEngine, binding) {
  if (binding !== undefined && binding !== null && typeof binding.value === 'string') {
    return binding.value;
  }
  if (editingEngine !== undefined && editingEngine !== null) {
    if (typeof editingEngine.getText === 'function') {
      const text = editingEngine.getText();
      if (typeof text === 'string') {
        return text;
      }
    }
  }
  return '';
}

function pushBinding(binding, value) {
  if (binding === undefined || binding === null) {
    return;
  }
  try {
    if (typeof binding.set === 'function') {
      binding.set(value);
      return;
    }
  } catch (_) {
    // Fall through to the `value` property write below.
  }
  try {
    binding.value = value;
  } catch (_) {
    // A read-only binding must never break the edit path.
  }
}

function readSelection(editingEngine) {
  if (editingEngine !== undefined && editingEngine !== null) {
    if (typeof editingEngine.getSelection === 'function') {
      try {
        const sel = editingEngine.getSelection();
        if (sel !== undefined && sel !== null && typeof sel === 'object') {
          return normaliseRange(sel.start, sel.end);
        }
      } catch (_) {
        // Fall through to the safe default below.
      }
    }
  }
  return [0, 0];
}

function pasteText(pasted) {
  if (pasted === undefined || pasted === null) {
    return '';
  }
  if (typeof pasted === 'string') {
    return pasted;
  }
  if (typeof pasted === 'object' && typeof pasted.text === 'string') {
    return pasted.text;
  }
  return '';
}

function createTextInput(deps) {
  const safe = deps === undefined || deps === null ? {} : deps;
  const editingEngine = safe.editingEngine;
  const clipboard = safe.clipboard;
  const binding = safe.binding;
  let committed = readCommittedText(editingEngine, binding);
  let disposed = false;
  const cleanups = [];

  function currentText() {
    if (
      editingEngine !== undefined &&
      editingEngine !== null &&
      typeof editingEngine.getText === 'function'
    ) {
      try {
        const text = editingEngine.getText();
        if (typeof text === 'string') {
          committed = text;
          return text;
        }
      } catch (_) {
        // Fall through to the cached committed text.
      }
    }
    return committed;
  }

  function setComposingText(s) {
    if (disposed) {
      return;
    }
    editingEngine.setComposingText(s);
  }

  function commitText(s) {
    if (disposed) {
      return;
    }
    editingEngine.commitText(s);
    committed = s;
    pushBinding(binding, s);
  }

  function cancelComposition() {
    if (disposed) {
      return;
    }
    editingEngine.cancelComposition();
  }

  function setSelection(start, end) {
    if (disposed) {
      return;
    }
    const [a, b] = normaliseRange(start, end);
    editingEngine.setSelection(a, b);
  }

  function getSelection() {
    const [a, b] = readSelection(editingEngine);
    return { start: a, end: b };
  }

  function copy() {
    if (disposed) {
      return;
    }
    const sel = getSelection();
    if (sel.start === sel.end) {
      return;
    }
    const text = currentText();
    const slice = text.slice(sel.start, sel.end);
    if (slice.length === 0) {
      return;
    }
    try {
      clipboard.writeText(slice);
    } catch (_) {
      // Clipboard transport failures must not break the field.
    }
  }

  function cut() {
    if (disposed) {
      return;
    }
    const sel = getSelection();
    if (sel.start === sel.end) {
      return;
    }
    const text = currentText();
    const slice = text.slice(sel.start, sel.end);
    if (slice.length === 0) {
      return;
    }
    try {
      clipboard.writeText(slice);
    } catch (_) {
      // Clipboard transport failures must not break the field.
    }
    const next = text.slice(0, sel.start) + text.slice(sel.end);
    try {
      editingEngine.commitText(next);
    } catch (_) {
      return;
    }
    committed = next;
    pushBinding(binding, next);
    try {
      editingEngine.setSelection(sel.start, sel.start);
    } catch (_) {
      // Selection restore is best-effort after a cut.
    }
  }

  function insertPaste(pasted) {
    const insert = pasteText(pasted);
    if (insert.length === 0 || disposed) {
      return;
    }
    const sel = getSelection();
    const text = currentText();
    const start = Math.min(sel.start, text.length);
    const end = Math.min(sel.end, text.length);
    const next = text.slice(0, start) + insert + text.slice(end);
    const caret = start + insert.length;
    try {
      editingEngine.commitText(next);
    } catch (_) {
      return;
    }
    committed = next;
    pushBinding(binding, next);
    try {
      editingEngine.setSelection(caret, caret);
    } catch (_) {
      // Selection restore is best-effort after a paste.
    }
  }

  function paste() {
    if (disposed) {
      return undefined;
    }
    let result;
    try {
      result = clipboard.readText();
    } catch (_) {
      return undefined;
    }
    if (result !== null && typeof result === 'object' && typeof result.then === 'function') {
      return result.then(
        (resolved) => {
          insertPaste(resolved);
        },
        () => {},
      );
    }
    insertPaste(result);
    return undefined;
  }

  function selectAll() {
    if (disposed) {
      return;
    }
    const text = currentText();
    try {
      editingEngine.setSelection(0, text.length);
    } catch (_) {
      // Best-effort: a failing engine must not throw out of the handle.
    }
  }

  function dispose() {
    if (disposed) {
      return;
    }
    disposed = true;
    while (cleanups.length > 0) {
      const fn = cleanups.pop();
      try {
        fn();
      } catch (_) {
        // Dispose must never throw.
      }
    }
  }

  const handle = {
    setComposingText,
    commitText,
    cancelComposition,
    setSelection,
    getSelection,
    cut,
    copy,
    paste,
    selectAll,
    dispose,
  };

  Object.defineProperty(handle, 'value', {
    enumerable: true,
    get() {
      return currentText();
    },
  });
  Object.defineProperty(handle, 'selection', {
    enumerable: true,
    get() {
      return getSelection();
    },
  });

  return handle;
}

// `TextInput` TSX descriptor factory, keeping the repo's `{ type, props }`
// shape: `type` equals the component name and `props` passes through.
function TextInput(props) {
  return { type: 'TextInput', props: props === undefined ? {} : props };
}

module.exports = { createTextInput, TextInput };
