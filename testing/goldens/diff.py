"""Pixel comparator for golden diffs (FR-TESTING-004 / AC-TESTING-035).

Pure-stdlib PNG decode via ``zlib`` + ``struct`` (no Pillow) and a
tolerance-gated comparator. Fixtures are self-written unfiltered 8-bit
RGBA PNGs; the decoder reconstructs all five PNG filter types so it
fails closed (raises) on corrupt input rather than silently passing.

Stdlib only. No wall-clock reads, no network, no device.
"""

import struct
import zlib

__all__ = ["pixel_diff"]

_PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _read_chunks(data):
    """Yield ``(kind, payload)`` tuples for a PNG byte string."""
    if data[:8] != _PNG_SIGNATURE:
        raise ValueError("not a PNG file (bad signature)")
    pos = 8
    while pos < len(data):
        if pos + 8 > len(data):
            raise ValueError("truncated PNG chunk header")
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        end = pos + 8 + length + 4
        if end > len(data):
            raise ValueError("truncated PNG chunk %r" % (kind,))
        payload = data[pos + 8 : pos + 8 + length]
        stored_crc, = struct.unpack(">I", data[pos + 8 + length : end])
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != stored_crc:
            raise ValueError("PNG chunk %r CRC mismatch" % (kind,))
        yield kind, payload
        pos = end
        if kind == b"IEND":
            break


def _paeth_predictor(left, up, upper_left):
    score = left + up - upper_left
    dist_left = abs(score - left)
    dist_up = abs(score - up)
    dist_upper = abs(score - upper_left)
    if dist_left <= dist_up and dist_left <= dist_upper:
        return left
    if dist_up <= dist_upper:
        return up
    return upper_left


def _decode_rgba_png(path):
    """Decode an 8-bit RGBA PNG file to ``(width, height, pixels)``.

    ``pixels`` is a flat ``bytes`` object of ``width * height * 4``
    channel values in row-major order. Raises on missing/corrupt input
    (fail closed — never returns a silent default).
    """
    with open(path, "rb") as handle:
        data = handle.read()
    width = height = None
    idat_parts = []
    seen_ihdr = False
    for kind, payload in _read_chunks(data):
        if kind == b"IHDR":
            if seen_ihdr:
                raise ValueError("duplicate IHDR in %r" % (path,))
            seen_ihdr = True
            if len(payload) != 13:
                raise ValueError("bad IHDR length in %r" % (path,))
            (width, height, bit_depth, color_type, comp, filt, interlace) = (
                struct.unpack(">IIBBBBB", payload)
            )
            if bit_depth != 8 or color_type != 6:
                raise ValueError(
                    "unsupported PNG format in %r "
                    "(need 8-bit RGBA)" % (path,)
                )
            if comp != 0 or filt != 0 or interlace != 0:
                raise ValueError("unsupported PNG encoding in %r" % (path,))
            if width <= 0 or height <= 0:
                raise ValueError("bad PNG dimensions in %r" % (path,))
        elif kind == b"IDAT":
            if not seen_ihdr:
                raise ValueError("IDAT before IHDR in %r" % (path,))
            idat_parts.append(payload)
        elif kind == b"IEND":
            break
    if not seen_ihdr or width is None:
        raise ValueError("PNG missing IHDR in %r" % (path,))
    if not idat_parts:
        raise ValueError("PNG missing IDAT in %r" % (path,))
    try:
        raw = zlib.decompress(b"".join(idat_parts))
    except zlib.error as exc:
        raise ValueError("PNG IDAT decompression failed for %r: %s" % (path, exc))
    stride = width * 4
    expected = height * (1 + stride)
    if len(raw) != expected:
        raise ValueError(
            "PNG scanline length mismatch in %r "
            "(got %d bytes, want %d)" % (path, len(raw), expected)
        )
    pixels = bytearray(width * height * 4)
    prev = bytearray(stride)
    pos = 0
    for row in range(height):
        filter_type = raw[pos]
        pos += 1
        scan = bytearray(raw[pos : pos + stride])
        pos += stride
        if filter_type == 0:  # None
            recon = scan
        elif filter_type == 1:  # Sub
            recon = bytearray(stride)
            for i in range(stride):
                left = recon[i - 4] if i >= 4 else 0
                recon[i] = (scan[i] + left) & 0xFF
        elif filter_type == 2:  # Up
            recon = bytearray(stride)
            for i in range(stride):
                recon[i] = (scan[i] + prev[i]) & 0xFF
        elif filter_type == 3:  # Average
            recon = bytearray(stride)
            for i in range(stride):
                left = recon[i - 4] if i >= 4 else 0
                recon[i] = (scan[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            recon = bytearray(stride)
            for i in range(stride):
                left = recon[i - 4] if i >= 4 else 0
                up = prev[i]
                upper_left = prev[i - 4] if i >= 4 else 0
                recon[i] = (
                    scan[i] + _paeth_predictor(left, up, upper_left)
                ) & 0xFF
        else:
            raise ValueError(
                "unknown PNG filter %d in %r" % (filter_type, path)
            )
        pixels[row * stride : (row + 1) * stride] = recon
        prev = recon
    return width, height, bytes(pixels)


def pixel_diff(a_path: str, b_path: str, tolerance: dict) -> dict:
    """Compare two PNG files under ``tolerance``.

    ``tolerance`` carries ``mean_per_channel_error`` (float) and
    ``max_differing_pixels`` (int) per the T7 tolerance store.
    Returns ``{flagged: bool, differing_pixels: int, mean_error: float}``
    where ``mean_error`` is the mean per-channel absolute error and
    ``flagged`` is true iff either tolerance bound is exceeded.

    Dimension mismatches are flagged (not raised); missing/corrupt
    inputs raise (fail closed). Pure stdlib — Pillow is never imported.
    """
    mean_bound = float(tolerance.get("mean_per_channel_error", 0.0))
    pixel_bound = int(tolerance.get("max_differing_pixels", 0))
    width_a, height_a, pixels_a = _decode_rgba_png(a_path)
    width_b, height_b, pixels_b = _decode_rgba_png(b_path)
    if (width_a, height_a) != (width_b, height_b):
        differing = max(width_a * height_a, width_b * height_b)
        return {
            "flagged": True,
            "differing_pixels": int(differing),
            "mean_error": 255.0,
        }
    total = 0
    differing = 0
    for offset in range(0, len(pixels_a), 4):
        delta = (
            abs(pixels_a[offset] - pixels_b[offset])
            + abs(pixels_a[offset + 1] - pixels_b[offset + 1])
            + abs(pixels_a[offset + 2] - pixels_b[offset + 2])
            + abs(pixels_a[offset + 3] - pixels_b[offset + 3])
        )
        total += delta
        if delta:
            differing += 1
    mean_error = float(total) / float(len(pixels_a)) if pixels_a else 0.0
    flagged = (differing > pixel_bound) or (mean_error > mean_bound)
    return {
        "flagged": bool(flagged),
        "differing_pixels": int(differing),
        "mean_error": float(mean_error),
    }
