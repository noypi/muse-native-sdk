"""Baseline Profile + snapshot active check (AC-021).

Fail closed: an empty or nonexistent build dir reports inactive (False)
and this function never raises.
"""

import os

# Release-active markers: at least one Baseline Profile artifact AND one
# snapshot artifact must be present under the build dir.
_PROFILE_MARKERS = (
    "baseline.prof",
    "baseline_profiles.txt",
    "baseline-profile.txt",
    "assets/dexopt/baseline.prof",
)

_SNAPSHOT_MARKERS = (
    "snapshot_blob.bin",
    "snapshot_blob_new.bin",
    "mmap_snapshot.bin",
)


def _has_marker(build_dir: str, markers) -> bool:
    for marker in markers:
        if os.path.isfile(os.path.join(build_dir, marker)):
            return True
    # Fall back to a shallow recursive scan for the bare filenames, so
    # Gradle/AGP layout variants are still recognised.
    wanted = {os.path.basename(m) for m in markers}
    try:
        for _root, _dirs, files in os.walk(build_dir):
            if wanted.intersection(files):
                return True
    except OSError:
        return False
    return False


def is_profile_active(build_dir: str) -> bool:
    """Return True when Baseline Profile + snapshot are active in build_dir."""
    try:
        if not build_dir or not os.path.isdir(build_dir):
            return False
        if not os.listdir(build_dir):
            return False
        return _has_marker(build_dir, _PROFILE_MARKERS) and _has_marker(
            build_dir, _SNAPSHOT_MARKERS)
    except Exception:
        return False
