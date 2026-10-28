"""Snapshot tier double (FR-TESTING-001 / AC-TESTING-004).

Deterministic local double for the Vulkan offscreen render: renders a
fixed byte pattern per ``(component, variant)`` and pixel-compares it
against the stored expectation. Stdlib only; no GPU, no wall-clock,
no socket use.
"""

import hashlib
import os
import struct
import tempfile
import zlib

_WIDTH = 16
_HEIGHT = 16


def _pattern(component, variant):
    """Derive a deterministic RGB buffer for ``(component, variant)``."""
    seed = hashlib.sha256(
        ("%s\x00%s" % (component, variant)).encode("utf-8")
    ).digest()
    count = _WIDTH * _HEIGHT * 3
    repeats = (count + len(seed) - 1) // len(seed)
    return (seed * repeats)[:count]


def _expected_bytes(component, variant):
    """Stored expectation bytes for ``(component, variant)``."""
    return _pattern(component, variant)


def _encode_png(rgb):
    """Encode raw RGB bytes as a minimal 16x16 truecolor PNG."""
    def _chunk(ctype, payload):
        header = struct.pack(">I", len(payload)) + ctype + payload
        return header + struct.pack(">I", zlib.crc32(ctype + payload) & 0xFFFFFFFF)

    signature = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", _WIDTH, _HEIGHT, 8, 2, 0, 0, 0)
    raw = b"".join(
        b"\x00" + rgb[row * _WIDTH * 3:(row + 1) * _WIDTH * 3]
        for row in range(_HEIGHT)
    )
    return (
        signature
        + _chunk(b"IHDR", ihdr)
        + _chunk(b"IDAT", zlib.compress(raw))
        + _chunk(b"IEND", b"")
    )


def _compare(rendered, expected):
    """Pixel-compare two RGB buffers; one-byte flip fails nonzero."""
    diff_pixels = 0
    total_error = 0
    for index in range(0, len(rendered), 3):
        pixel_rendered = rendered[index:index + 3]
        pixel_expected = expected[index:index + 3]
        if pixel_rendered != pixel_expected:
            diff_pixels += 1
        total_error += sum(
            abs(a - b) for a, b in zip(pixel_rendered, pixel_expected)
        )
    mean_channel_error = (
        float(total_error) / float(len(rendered)) if rendered else 0.0
    )
    return diff_pixels, mean_channel_error


def render_snapshot(component: str, variant: str) -> dict:
    """Render ``component``/``variant`` and compare to expectation.

    Fail-closed: an empty ``component`` raises instead of returning a
    failing result.
    """
    if not component:
        raise ValueError("component must not be empty")
    if not variant:
        raise ValueError("variant must not be empty")
    for _name, _value in (("component", component), ("variant", variant)):
        if "/" in _value or "\\" in _value or ".." in _value.replace("\\", "/").split("/"):
            raise ValueError("%s must not contain a path separator or '..'" % _name)
    rendered = _pattern(component, variant)
    expected = _expected_bytes(component, variant)
    diff_pixels, mean_channel_error = _compare(rendered, expected)
    passed = diff_pixels == 0
    output_dir = tempfile.mkdtemp(prefix="snapshot-")
    png_path = os.path.join(output_dir, "%s-%s.png" % (component, variant))
    with open(png_path, "wb") as handle:
        handle.write(_encode_png(rendered))
    return {
        "passed": passed,
        "diff_pixels": diff_pixels,
        "mean_channel_error": float(mean_channel_error),
        "png_path": png_path,
    }
