"""Shared dependency data for the deps checks (FR-DEPS-002..007).

Transcribes the closed allow-list and deny-list from
``manual-context.local/specs-cpp-dependencies.md`` so the check scripts
(T2-T7) share a single source instead of drifting copies.

No CLI here; T2-T7 import :func:`get_allowlist`, :func:`get_denylist`
and :func:`load_manifest`.
"""

import json
import os

__all__ = [
    "DepsDataError",
    "get_allowlist",
    "get_cronet_pins",
    "get_denylist",
    "load_manifest",
]


class DepsDataError(Exception):
    """Raised when a dependency manifest is missing or unparseable.

    Fail-closed contract (plan A1): callers treat this as denial,
    never as an empty/assumed-permissive list.
    """


# Allow-list mirroring the spec table (library, need, owner).
_ALLOWLIST = [
    ("Hermes", "JS execution (CORE-008)", "core"),
    ("QuickJS", "JS execution (CORE-008); low-memory build", "core"),
    ("Yoga", "Layout (LAYOUT-001)", "layout"),
    ("Cassowary", "Layout (LAYOUT-001); sliver/masonry delegate", "layout"),
    ("HarfBuzz", "Text shaping", "text"),
    ("ICU4C", "Internationalization for the text engine", "text"),
    ("FreeType", "Glyph rasterization for the text engine", "text"),
    ("Vulkan Memory Allocator (VMA)", "GPU allocation (GFX-*)", "graphics"),
    ("vk-bootstrap", "Device selection convenience", "graphics"),
    ("Vulkan-Hpp", "Vulkan C++ API convenience", "graphics"),
    ("libswappy", "Frame pacing", "graphics"),
    ("FlatBuffers", "Zero-copy payloads (CORE-006); versioned schema registry", "core"),
    ("Crashpad", "Minidumps (CORE-004)", "core"),
    ("Oboe", "Audio output shim", "media"),
    ("AAudio", "Audio output on API 29+", "media"),
    ("Media3", "Playback and DRM", "media"),
    ("FFmpeg", "Codecs behind a feature module", "media"),
    ("WebRTC", "Real-time transport; dynamic feature module only", "media"),
    ("Lottie", "Vector animation import (FR-MOTION-010); importer only", "motion"),
    ("Rive", "Vector animation import (FR-MOTION-010); importer only", "motion"),
    ("Play Core NDK", "Feature/Asset Delivery", "build"),
    (
        "moodycamel::ConcurrentQueue",
        "MPMC queue for the worker pool (CORE-003); header-only, no static init",
        "core",
    ),
    (
        "ankerl::unordered_dense",
        "Hash maps in hot paths (CORE-003, list diffing); header-only, MIT. "
        "Erase moves the last element onto the vacated slot, so references and "
        "iterators are invalidated by insert and erase; permitted only under "
        "RULE-DEPS-006, with identity stored as a handle",
        "core",
    ),
    ("{fmt}", "Formatting where libc++ lacks std::format", "core"),
    ("magic_enum", "Enum names for diagnostics; header-only", "core"),
    ("simdjson", "Tooling/debug JSON only; never in the frame path", "build"),
    ("Cronet", "Default shippable transport (FR-NET-014)", "net"),
]

# Deny-list mirroring the spec table (library, reason).
_DENYLIST = [
    (
        "Boost (all)",
        "Dynamic initializers (filesystem, regex, iostreams, log, "
        "program_options) violate CORE-012; assumes exceptions where the render "
        "path is -fno-exceptions (CORE-001-R2); header-only instantiation cost "
        "fights the APK budget and build determinism (footprint and "
        "compile-time cost); C++20 plus std::pmr already covers the useful subset",
    ),
    (
        "Folly",
        "Very heavy, assumes a large Facebook-specific dependency set",
    ),
    (
        "EASTL",
        "Replaces libc++ containers and conflicts with the NDK's libc++ and "
        "the std::pmr allocator plan (CORE-009)",
    ),
    (
        "Qt, POCO",
        "Framework-sized; no part of the problem they solve is unmet",
    ),
    (
        "Abseil (all)",
        "Duplicated by C++20 (std::span, std::string_view, std::optional, "
        "std::variant, std::pmr) and by ICU for time zones; framework-sized "
        "build surface for the one attractive header; absl::flat_hash_map "
        "shares the flat-map instability RULE-DEPS-006 already constrains, and "
        "ankerl::unordered_dense fills that role",
    ),
    (
        "nlohmann/json in production paths",
        "Size and allocation behaviour; tooling use only",
    ),
]


def get_allowlist():
    """Return the allow-list as ``[{name, owner, need}]`` (a fresh copy)."""
    return [
        {"name": name, "owner": owner, "need": need}
        for name, need, owner in _ALLOWLIST
    ]


# Cronet pins transcribed verbatim from FR-DEPS-010 Notes (no fallback pins
# per Q-DEPS-004).
_CRONET_PINS = [
    {
        "coordinate": "org.chromium.net:cronet-bundled:500.1.0",
        "version": "500.1.0",
        "sha256": (
            "fa286aeebe66dcab4a64df017d566cb8ec563c3bb63af527997c3a4e056bf295"
        ),
        "url": (
            "https://maven.google.com/org/chromium/net/cronet-bundled/500.1.0/"
            "cronet-bundled-500.1.0.aar"
        ),
        "licence": "BSD-3-Clause",
        "licence_url": (
            "https://chromium.googlesource.com/chromium/src/+/HEAD/LICENSE"
        ),
    },
    {
        "coordinate": "org.chromium.net:cronet:500.1.0",
        "version": "500.1.0",
        "sha256": (
            "d0cbb75a3e558166a1e21747d75b788b2650ddf8bec321b727b0878f01addcf8"
        ),
        "url": (
            "https://maven.google.com/org/chromium/net/cronet/500.1.0/"
            "cronet-500.1.0.aar"
        ),
        "licence": "BSD-3-Clause",
        "licence_url": (
            "https://chromium.googlesource.com/chromium/src/+/HEAD/LICENSE"
        ),
    },
]


def get_cronet_pins():
    """Return the Cronet AAR pins as a fresh list of pin dicts."""
    return [dict(pin) for pin in _CRONET_PINS]


def get_denylist():
    """Return the deny-list as ``[{name, reason}]`` (a fresh copy)."""
    return [{"name": name, "reason": reason} for name, reason in _DENYLIST]


def load_manifest(path):
    """Load a dependency manifest file, failing closed.

    Accepts either a bare JSON list of entry dicts or an object of the
    form ``{"dependencies": [...]}`` (the ``build/dependency-manifest.json``
    shape).  Raises :class:`DepsDataError` when the file is missing,
    unparseable, or does not hold a list of dicts.
    """
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except FileNotFoundError as exc:
        raise DepsDataError("manifest not found: %s" % path) from exc
    except (OSError, ValueError) as exc:
        raise DepsDataError("manifest unparseable: %s (%s)" % (path, exc)) from exc
    if isinstance(data, dict):
        if "dependencies" not in data:
            raise DepsDataError(
                "manifest unparseable: %s (object without 'dependencies')" % path
            )
        data = data["dependencies"]
    if not isinstance(data, list) or any(
        not isinstance(entry, dict) for entry in data
    ):
        raise DepsDataError(
            "manifest unparseable: %s (expected a list of objects)" % path
        )
    return data


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit("no CLI here; see tools/deps/*_check.py (T2-T7)")
