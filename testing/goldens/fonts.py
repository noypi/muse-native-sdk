"""Frozen font corpus gate (FR-TESTING-004 / AC-TESTING-033).

Stdlib only. The manifest pins ``{file, version, sha256}`` per font plus
the shaping/locale settings imported from the TEST-004 determinism
contract (``testing.determinism`` pinned environment). No host font is
ever read: any entry that resolves outside the manifest's own corpus
directory (absolute path or ``..`` escape) is rejected as host fallback
without being opened.
"""

import hashlib
import json
import os

from testing.determinism.prng import pinned_environment

SHAPING_ENGINE = "harfbuzz"

SHAPING_SETTINGS = {
    "engine": SHAPING_ENGINE,
    "hinting": "none",
    "antialias": "grayscale",
}


def _pinned_text_locale():
    """Shaping locale pinned by the TEST-004 determinism contract."""
    with pinned_environment(0) as descriptors:
        return {
            "locale": str(descriptors["locale"]),
            "timezone": str(descriptors["timezone"]),
        }


FROZEN_TEXT_SETTINGS = {
    "shaping": dict(SHAPING_SETTINGS),
    "locale": _pinned_text_locale(),
}

_FONT_EXTENSIONS = (".ttf", ".otf", ".woff", ".woff2")

__all__ = [
    "FROZEN_TEXT_SETTINGS",
    "SHAPING_ENGINE",
    "SHAPING_SETTINGS",
    "assert_frozen_fonts",
]


def _manifest_digest(entries):
    """Digest scheme: sha256 hex of "\\n".join("file:version:sha256")."""
    lines = sorted(
        "%s:%s:%s" % (entry["file"], entry["version"], entry["sha256"])
        for entry in entries
    )
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest()


def _corpus_path(manifest_path, name):
    """Resolve ``name`` inside the manifest's corpus directory.

    Returns ``None`` when ``name`` is absolute or escapes the corpus
    directory (host fallback); the caller rejects it without opening it.
    """
    base = os.path.dirname(os.path.abspath(manifest_path))
    if os.path.isabs(name):
        return None
    joined = os.path.normpath(os.path.join(base, name))
    if joined == base or not joined.startswith(base + os.sep):
        return None
    return joined


def assert_frozen_fonts(manifest_path: str) -> dict:
    """Assert the frozen font corpus pinned by ``manifest_path``.

    Returns ``{frozen: bool, fonts: list[str], digest: str}``. Raises
    ``AssertionError`` naming the offending font on any host-fallback
    entry or manifest mismatch; malformed JSON propagates as
    ``ValueError`` (``json.JSONDecodeError``).
    """
    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if not isinstance(manifest, dict):
        raise AssertionError(
            "font manifest %r is malformed: top-level JSON value "
            "must be an object" % (manifest_path,)
        )
    if manifest.get("frozen") is not True:
        raise AssertionError(
            "font manifest %r is not frozen: \"frozen\": true "
            "is required (TC-TESTING-033)" % (manifest_path,)
        )
    entries = manifest.get("fonts")
    if not isinstance(entries, list) or not entries:
        raise AssertionError(
            "font manifest %r declares no fonts" % (manifest_path,)
        )
    for entry in entries:
        if not isinstance(entry, dict):
            raise AssertionError(
                "font manifest %r has a malformed font entry: %r"
                % (manifest_path, entry)
            )
        for key in ("file", "version", "sha256"):
            if not entry.get(key):
                raise AssertionError(
                    "font manifest %r has a font entry missing %r: %r"
                    % (manifest_path, key, entry)
                )
        name = str(entry["file"])
        path = _corpus_path(manifest_path, name)
        if path is None:
            raise AssertionError(
                "host-fallback font %r in %r: fonts must resolve "
                "inside the frozen corpus, never to a host path"
                % (name, manifest_path)
            )
        if not os.path.isfile(path):
            raise AssertionError(
                "frozen font %r is missing from the corpus of %r"
                % (name, manifest_path)
            )
        with open(path, "rb") as handle:
            actual = hashlib.sha256(handle.read()).hexdigest()
        if actual != entry["sha256"]:
            raise AssertionError(
                "frozen font %r content mismatch in %r: manifest "
                "pins %s but the corpus file hashes to %s"
                % (name, manifest_path, entry["sha256"], actual)
            )
    stored = manifest.get("digest")
    expected = _manifest_digest(entries)
    if stored != expected:
        base = os.path.dirname(os.path.abspath(manifest_path))
        listed = {str(entry["file"]) for entry in entries}
        try:
            candidates = sorted(os.listdir(base))
        except OSError:
            candidates = []
        orphans = [
            name
            for name in candidates
            if name not in listed
            and name != os.path.basename(manifest_path)
            and name.lower().endswith(_FONT_EXTENSIONS)
        ]
        raise AssertionError(
            "font manifest %r digest mismatch (stored %r, computed %r): "
            "an entry was added, removed, or re-pinned without "
            "re-freezing; pinned fonts: %s; off-manifest corpus "
            "fonts: %s"
            % (
                manifest_path,
                stored,
                expected,
                sorted(listed),
                orphans,
            )
        )
    return {
        "frozen": True,
        "fonts": sorted(str(entry["file"]) for entry in entries),
        "digest": str(stored),
    }
