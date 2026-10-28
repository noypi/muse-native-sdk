#!/usr/bin/env python3
"""Gallery motion-map check (FR-MOTION-015, AC-MOTION-049).

Parses ``docs/motion/component-map.md`` (T7 evidence) against the gallery's
component inventory and fails closed: exit 0 only when every gallery
component/state-change has a map row naming known T1 tokens; exit 1 naming
the component+state-change (or row/line) on a missing row, unknown token,
malformed row, or missing map file. An unmapped component never resolves a
silent default token. Stdlib only.

The expected inventory below was derived read-only from
``examples/m3-gallery/`` (``muse-native.yaml`` + ``src/`` story list, fixture
owned by run fr-tsx-008, never modified here) together with the T7 map's
per-component/state-change rows; each run additionally re-scans the gallery
dir read-only so a newly added story naming an unmapped component fails.
The component manifest ``packages/native/muse.manifest.json`` is the
authority for which components the gallery showcases: a ``src/`` story file
naming something absent from the manifest (another run's throwaway fixture)
is ignored with a stdout warning naming the file instead of hard-failing.

Row text is data only: the parser uses plain string splitting and never
evals, execs, or shells out row content.
"""

import argparse
import glob
import json
import os
import re
import sys

DEFAULT_MAP = os.path.join("docs", "motion", "component-map.md")
DEFAULT_GALLERY = os.path.join("examples", "m3-gallery")
DEFAULT_MANIFEST = os.path.join("packages", "native", "muse.manifest.json")

# Verified T1 token-name set, hardcoded from the docs/motion/component-map.md
# header (scheme version 1). Name-membership only; the engine is NOT imported.
KNOWN_TOKENS = frozenset([
    "duration-short1",
    "duration-short2",
    "duration-medium1",
    "duration-medium2",
    "duration-long1",
    "duration-long2",
    "duration-long3",
    "duration-long4",
    "easing-standard",
    "easing-emphasized",
    "easing-accelerate",
    "easing-decelerate",
    "spatial-default",
    "effects-default",
])

STATIC_MARKER = "none (static)"

# Pinned component inventory: every (Component, State change) pair from the
# staged T7 map covering the gallery stories. A pair absent from the checked
# file is a hard failure, never a silent default.
EXPECTED_ROWS = frozenset([
    ("Surface", "pressed"),
    ("Surface", "selected"),
    ("Text", "\u2014"),
    ("Icon", "pressed"),
    ("Icon", "\u2014"),
    ("Image", "entered"),
    ("Ripple/state layer", "pressed"),
    ("Divider", "\u2014"),
    ("Scroll", "expanded"),
    ("Scroll", "dismissed"),
    ("Stack/Flex", "\u2014"),
    ("Text-field edit core", "pressed"),
    ("Text-field edit core", "entered"),
    ("OverlayHost", "entered"),
    ("OverlayHost", "dismissed"),
    ("Text input", "pressed"),
    ("Text input", "entered"),
    ("Button (all variants)", "pressed"),
    ("Button (all variants)", "selected"),
    ("IconButton", "pressed"),
    ("IconButton", "selected"),
    ("FAB", "pressed"),
    ("FAB", "entered"),
    ("FAB", "dismissed"),
    ("Extended FAB", "pressed"),
    ("Extended FAB", "expanded"),
    ("Extended FAB", "dismissed"),
    ("Card (all variants)", "pressed"),
    ("Card (all variants)", "selected"),
    ("Card (all variants)", "entered"),
    ("Chip (all variants)", "pressed"),
    ("Chip (all variants)", "selected"),
    ("Chip (all variants)", "dismissed"),
    ("Checkbox", "pressed"),
    ("Checkbox", "selected"),
    ("Radio", "pressed"),
    ("Radio", "selected"),
    ("Switch", "pressed"),
    ("Switch", "selected"),
    ("Slider", "pressed"),
    ("Slider", "selected"),
    ("Badge", "entered"),
    ("Badge", "dismissed"),
    ("LinearProgress", "entered"),
    ("LinearProgress", "dismissed"),
    ("CircularProgress", "entered"),
    ("CircularProgress", "dismissed"),
    ("ListItem", "pressed"),
    ("ListItem", "selected"),
    ("ListItem", "expanded"),
    ("Avatar", "\u2014"),
    ("Tooltip", "entered"),
    ("Tooltip", "dismissed"),
    ("TopAppBar (all sizes)", "expanded"),
    ("TopAppBar (all sizes)", "entered"),
    ("NavigationBar", "selected"),
    ("NavigationRail", "selected"),
    ("NavigationDrawer", "expanded"),
    ("NavigationDrawer", "dismissed"),
    ("NavigationDrawer", "selected"),
    ("SegmentedButton", "selected"),
    ("SegmentedButton", "pressed"),
    ("Dialog", "entered"),
    ("Dialog", "dismissed"),
    ("BottomSheet", "expanded"),
    ("BottomSheet", "dismissed"),
    ("Menu", "expanded"),
    ("Menu", "dismissed"),
    ("Menu", "selected"),
    ("SnackbarHost", "entered"),
    ("SnackbarHost", "dismissed"),
    ("SearchBar", "pressed"),
    ("SearchBar", "expanded"),
    ("SearchBar", "dismissed"),
    ("Carousel", "expanded"),
    ("Carousel", "selected"),
    ("BottomAppBar", "entered"),
    ("BottomAppBar", "dismissed"),
    ("ButtonGroup", "pressed"),
    ("ButtonGroup", "selected"),
    ("SplitButton", "pressed"),
    ("SplitButton", "expanded"),
])

_BACKTICK_SPAN = re.compile(r"`([^`]*)`")
_SEPARATOR_LINE = re.compile(r"^[\s|:\-]+$")


class MapError(Exception):
    """A fail-closed map defect with an already-formatted message."""


def _fail(message):
    raise MapError(message)


def parse_map(text):
    """Parse map Markdown table rows; return list of dicts with line numbers.

    Row text is treated as pure data (split/strip only). Malformed rows
    (wrong column count, empty Component/State-change cell) raise MapError
    naming the line number.
    """
    rows = []
    for lineno, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        if _SEPARATOR_LINE.match(stripped):
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if len(cells) != 4:
            _fail("motion_map_check: malformed row at line %d: "
                  "expected 4 columns, found %d" % (lineno, len(cells)))
        component, state, tokens, _notes = cells
        if component.lower() == "component":
            continue  # header row
        if not component:
            _fail("motion_map_check: malformed row at line %d: "
                  "empty Component cell" % lineno)
        if not state:
            _fail("motion_map_check: malformed row at line %d: "
                  "empty State-change cell for component %r"
                  % (lineno, component))
        rows.append({"lineno": lineno, "component": component,
                     "state": state, "tokens": tokens})
    return rows


def row_tokens(tokens_cell):
    """Extract candidate token names from a Token(s) cell (data only)."""
    spans = _BACKTICK_SPAN.findall(tokens_cell)
    if spans:
        return [span.strip() for span in spans]
    candidate = tokens_cell.strip()
    if candidate:
        return [candidate]
    return []


def check_tokens(rows):
    """Fail on the first row naming a token outside the T1 set."""
    for row in rows:
        names = row_tokens(row["tokens"])
        if not names:
            _fail("motion_map_check: malformed row at line %d: "
                  "empty Token(s) cell for component %r state-change %r"
                  % (row["lineno"], row["component"], row["state"]))
        for name in names:
            if name == STATIC_MARKER:
                continue
            if name not in KNOWN_TOKENS:
                _fail("motion_map_check: unknown token %r in row for "
                      "component %r state-change %r (line %d)"
                      % (name, row["component"], row["state"],
                         row["lineno"]))


def check_coverage(rows):
    """Fail naming every expected component+state-change missing a row."""
    found = frozenset((row["component"], row["state"]) for row in rows)
    missing = sorted(EXPECTED_ROWS - found)
    if missing:
        for component, state in missing:
            print("motion_map_check: missing mapping for component %r "
                  "state-change %r (no silent default applied)"
                  % (component, state), file=sys.stderr)
        _fail("motion_map_check: %d component/state-change row(s) missing"
              % len(missing))


def load_manifest_components(manifest_path):
    """Read-only load of the manifest authority for gallery components.

    Returns the normalized set of component names listed under the manifest's
    ``components`` key (``name``/``aliases``/``components`` object), or None
    when the manifest cannot be read or parsed (a stderr warning is emitted
    and callers fall back to unfiltered hints, never a silent default).
    Row text and manifest text are data only: parsed with json/string
    splitting, never evaled, execed, or shelled out.
    """
    try:
        with open(manifest_path, encoding="utf-8") as handle:
            manifest = json.load(handle)
    except (OSError, ValueError) as exc:
        print("motion_map_check: warning: cannot read component manifest: %s"
              % exc, file=sys.stderr)
        return None
    if not isinstance(manifest, dict):
        print("motion_map_check: warning: cannot read component manifest: "
              "expected a JSON object in %r" % manifest_path, file=sys.stderr)
        return None
    entries = manifest.get("components")
    if not isinstance(entries, list):
        print("motion_map_check: warning: cannot read component manifest: "
              "missing components list in %r" % manifest_path,
              file=sys.stderr)
        return None
    names = set()
    for entry in entries:
        if isinstance(entry, dict) and isinstance(entry.get("name"), str):
            names.add(_QUALIFIER_PAREN.sub("", entry["name"]).strip().lower())
    return names


def scan_gallery(gallery_dir, manifest_path=DEFAULT_MANIFEST):
    """Read-only scan of the gallery fixture for story component hints.

    Returns lowercase component-name hints derived from muse-native.yaml and
    the src/ story list without modifying anything. Unreadable gallery input
    yields no hints (the pinned EXPECTED_ROWS inventory stays authoritative).

    The component manifest (``packages/native/muse.manifest.json``) is the
    authority for which components the gallery showcases: only manifest-listed
    components participate in the hint backstop. A src/ story file naming
    something absent from the manifest (another run's throwaway fixture) is
    ignored with a stdout warning naming the file instead of hard-failing.
    """
    allowed = load_manifest_components(manifest_path)
    hints = set()
    try:
        with open(os.path.join(gallery_dir, "muse-native.yaml"),
                  encoding="utf-8") as handle:
            for line in handle.read().splitlines():
                stripped = line.strip()
                if stripped and not stripped.startswith("#") and \
                        ":" not in stripped:
                    hints.add(stripped.lower())
    except OSError as exc:
        print("motion_map_check: warning: cannot read gallery manifest: %s"
              % exc, file=sys.stderr)
    try:
        stories = glob.glob(os.path.join(gallery_dir, "src", "**", "*.tsx"),
                            recursive=True)
    except OSError as exc:  # pragma: no cover - glob rarely raises
        print("motion_map_check: warning: cannot list gallery stories: %s"
              % exc, file=sys.stderr)
        stories = []
    for path in stories:
        base = os.path.basename(path)
        if base.endswith(".tsx"):
            base = base[:-len(".tsx")]
        if base.lower().endswith("screen"):
            base = base[:-len("screen")]
        if base and base.lower() != "app":
            hint = base.lower()
            if allowed is not None and \
                    _QUALIFIER_PAREN.sub("", hint).strip().lower() \
                    not in allowed:
                print("motion_map_check: warning: ignoring gallery story "
                      "file %r: component %r is not listed in the component "
                      "manifest" % (path, base))
                continue
            hints.add(hint)
    if allowed is not None:
        hints = set(hint for hint in hints
                    if _QUALIFIER_PAREN.sub("", hint).strip().lower()
                    in allowed)
    return hints


_QUALIFIER_PAREN = re.compile(r"[(].*?[)]")


def normalize_component(name):
    """Normalize a map Component cell to its base component name.

    Variant qualifiers are stripped explicitly (e.g. "Button (all variants)"
    -> "button", "TopAppBar (all sizes)" -> "topappbar"); comparison is
    exact on the normalized name, never substring.
    """
    return _QUALIFIER_PAREN.sub("", name).strip().lower()


def check_gallery_hints(hints, rows):
    """Fail when a gallery story names a component with no map row at all.

    Best-effort backstop only: the pinned EXPECTED_ROWS inventory enforced
    by check_coverage stays authoritative. Matching is exact on normalized
    base component names (variant qualifiers stripped explicitly), so a
    hint that is merely a substring of a mapped row still fails instead
    of passing silently.
    """
    components = frozenset(normalize_component(row["component"])
                           for row in rows)
    for hint in sorted(hints):
        if normalize_component(hint) not in components:
            _fail("motion_map_check: gallery story names unmapped "
                  "component %r (no silent default applied)" % hint)


def check_map(map_path, gallery_dir, manifest_path=DEFAULT_MANIFEST):
    """Run all checks; raise MapError on the first failure."""
    try:
        with open(map_path, encoding="utf-8") as handle:
            text = handle.read()
    except OSError as exc:
        _fail("motion_map_check: missing map file %r: %s" % (map_path, exc))
    rows = parse_map(text)
    if not rows:
        _fail("motion_map_check: no map rows found in %r" % map_path)
    check_tokens(rows)
    check_coverage(rows)
    check_gallery_hints(scan_gallery(gallery_dir, manifest_path), rows)
    return len(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Fail-closed check that every gallery "
                    "component/state-change has a motion-map row.")
    parser.add_argument("--map", default=DEFAULT_MAP,
                        help="path to component-map.md "
                             "(default: %(default)s)")
    parser.add_argument("--gallery", default=DEFAULT_GALLERY,
                        help="gallery fixture dir, read-only "
                             "(default: %(default)s)")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST,
                        help="component manifest path, read-only "
                             "(default: %(default)s)")
    args = parser.parse_args(argv)

    try:
        count = check_map(args.map, args.gallery, args.manifest)
    except MapError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    print("motion_map_check: ok: all %d map rows cover the gallery inventory"
          % count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
