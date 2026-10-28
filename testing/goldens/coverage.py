"""Golden coverage over the M3 matrix (FR-TESTING-004 / AC-TESTING-032).

Stdlib only. No wall-clock reads, no network, no device.
Goldens are partitioned by driver class at render time.
"""

import hashlib
import json
import os
import shutil
import tempfile

from testing.cli.snapshot import render_snapshot
from testing.determinism.prng import pinned_environment

VARIANTS = ("light", "dark", "rtl")

__all__ = ["VARIANTS", "golden_coverage", "render_golden", "render_snapshot"]


def _component_names(matrix):
    names = []
    for entry in matrix.get("components", []):
        if isinstance(entry, dict):
            name = entry.get("name", "")
        else:
            name = str(entry)
        if name:
            names.append(str(name))
    return names


def _golden_path(component, variant):
    return os.path.join("testing", "goldens", "%s-%s.png" % (component, variant))


def golden_coverage(matrix_path: str) -> dict:
    """Report golden coverage for every component x variant."""
    with open(matrix_path, "r", encoding="utf-8") as handle:
        matrix = json.load(handle)
    names = _component_names(matrix)
    variants = list(VARIANTS)
    goldens = []
    missing = []
    for component in names:
        for variant in variants:
            path = _golden_path(component, variant)
            if os.path.isfile(path):
                goldens.append(path)
            else:
                missing.append(path)
    return {
        "components": len(names),
        "variants": variants,
        "missing": missing,
        "goldens": goldens,
    }


def _validate_name(label, value):
    if not value:
        raise ValueError("%s must not be empty" % label)
    text = str(value)
    if "/" in text or "\\" in text or ".." in text.split("/"):
        raise ValueError(
            "%s must not contain a path separator or '..'" % label
        )
    if ".." in text.split("\\"):
        raise ValueError(
            "%s must not contain a path separator or '..'" % label
        )
    return text


def render_golden(component: str, variant: str, driver_class: str) -> dict:
    """Render ``component``/``variant`` for ``driver_class``.

    Delegates to :func:`testing.cli.snapshot.render_snapshot` with
    variant+driver_class pinned via the ``testing.determinism`` pinned
    environment. Goldens are partitioned by driver class: each driver
    class gets a distinct ``png_path``.
    """
    component = _validate_name("component", component)
    if variant not in VARIANTS:
        raise ValueError("unknown variant: %r" % (variant,))
    driver_class = _validate_name("driver_class", driver_class)
    seed = (
        int(
            hashlib.sha256(
                ("%s\x00%s\x00%s" % (component, variant, driver_class)).encode(
                    "utf-8"
                )
            ).hexdigest(),
            16,
        )
        % (2**31)
    )
    with pinned_environment(seed):
        snap = render_snapshot(component, variant)
    src = snap["png_path"]
    output_dir = tempfile.mkdtemp(prefix="golden-")
    dst = os.path.join(
        output_dir, "%s-%s-%s.png" % (component, variant, driver_class)
    )
    shutil.copyfile(src, dst)
    return {
        "png_path": dst,
        "variant": variant,
        "driver_class": driver_class,
    }
