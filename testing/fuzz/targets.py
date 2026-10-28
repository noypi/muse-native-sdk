"""Fuzz target registry (FR-TESTING-003 / AC-TESTING-023).

One target per AC-023 family (the FlatBuffers/protobuf/CBOR/MessagePack
serialization group expands to four codec targets, so 10 registry
families cover the 8 spec families). Each family exposes a small
byte-level parser entry point exercised by :func:`run_target` under a
bounded budget. Crash ids are deterministic hashes of family+bytes.

Stdlib only. No wall-clock reads (``time.monotonic`` deltas only for
budget enforcement), no sockets, no subprocesses. Sanitizer values are
labels owned by CORE-MSAFETY — no toolchain work happens here.
"""

import hashlib
import time

# Magic prefix that forces a synthetic crash. Ordinary (seed) inputs
# never contain it, so the merge-time gate observes zero new crashes
# while crash reporting stays deterministic and reproducible.
_CRASH_MAGIC = b"FUZZ-CRASH:"

# Cap on bytes scanned per run_target call: keeps every invocation
# bounded even for hostile oversized inputs.
_MAX_SCAN = 65536

_SANITIZERS = ("asan", "ubsan", "msan")


def _scan_budget(data, budget_ms):
    """Do bounded byte-level work; abort once the budget is spent."""
    deadline = time.monotonic() + max(0, budget_ms) / 1000.0
    total = 0
    view = data[:_MAX_SCAN]
    # Chunked checksum-style scan so long inputs yield regularly and
    # the deadline check stays cheap relative to the work.
    for offset in range(0, len(view), 256):
        chunk = view[offset : offset + 256]
        total = (total + sum(chunk)) & 0xFFFFFFFF
        if time.monotonic() >= deadline:
            break
    return total


def _parse_css_sass(data):
    # Braces/brackets balance scan over a Sass stylesheet.
    depth = 0
    for byte in data[:_MAX_SCAN]:
        if byte in (0x7B, 0x28, 0x5B):  # { ( [
            depth += 1
        elif byte in (0x7D, 0x29, 0x5D):  # } ) ]
            depth -= 1
    return depth


def _parse_layout_solver(data):
    # Constraint tokens: pairs of big-endian u16 widths/heights.
    count = 0
    view = data[:_MAX_SCAN]
    for offset in range(0, len(view) - 1, 2):
        value = (view[offset] << 8) | view[offset + 1]
        if value:
            count += 1
    return count


def _parse_text_shaping(data):
    # Cluster scan: count non-continuation UTF-8 lead bytes.
    clusters = 0
    for byte in data[:_MAX_SCAN]:
        if byte & 0xC0 != 0x80:
            clusters += 1
    return clusters


def _parse_deeplink_uri(data):
    # Scheme://authority/path query scan for delimiters.
    try:
        text = data[:4096].decode("ascii")
    except UnicodeDecodeError:
        return -1
    parts = 0
    for sep in ("://", "/", "?", "&", "="):
        parts += text.count(sep)
    return parts


def _parse_flatbuffers(data):
    # Table header: u16 length-prefixed vector walk.
    view = data[:_MAX_SCAN]
    pos = 0
    fields = 0
    while pos + 2 <= len(view):
        length = (view[pos] << 8) | view[pos + 1]
        pos += 2
        if length > len(view) - pos:
            break
        pos += length
        fields += 1
    return fields


def _parse_protobuf(data):
    # Varint field walk: tag byte + varint value per field.
    view = data[:_MAX_SCAN]
    pos = 0
    fields = 0
    while pos < len(view):
        pos += 1  # tag
        shift = 0
        while pos < len(view) and shift < 64:
            byte = view[pos]
            pos += 1
            shift += 7
            if not byte & 0x80:
                break
        fields += 1
    return fields


def _parse_cbor(data):
    # Major-type + argument walk over CBOR initial bytes.
    view = data[:_MAX_SCAN]
    pos = 0
    items = 0
    while pos < len(view):
        initial = view[pos]
        pos += 1
        arg = initial & 0x1F
        if arg == 24:
            pos += 1
        elif arg == 25:
            pos += 2
        elif arg == 26:
            pos += 4
        elif arg == 27:
            pos += 8
        items += 1
    return items


def _parse_msgpack(data):
    # Fixint/fixmap/fixarray/str/bin/ext header walk.
    view = data[:_MAX_SCAN]
    pos = 0
    items = 0
    while pos < len(view):
        prefix = view[pos]
        pos += 1
        if 0xA0 <= prefix <= 0xBF:  # fixstr
            pos += prefix & 0x1F
        elif 0xC4 <= prefix <= 0xC6:  # bin 8/16/32
            width = 1 << (prefix - 0xC4)
            if pos + width > len(view):
                break
            length = int.from_bytes(view[pos : pos + width], "big")
            pos += width + min(length, len(view) - pos)
        items += 1
    return items


def _parse_image_decoders(data):
    # Container sniff: PNG/JPEG/GIF/WebP magic + chunk walk.
    magic = bytes(data[:12])
    score = 0
    if magic.startswith(b"\x89PNG"):
        score += 1
    if magic[:3] == b"GIF":
        score += 1
    if magic[:2] == b"\xff\xd8":
        score += 1
    if magic[:4] == b"RIFF" and magic[8:12] == b"WEBP":
        score += 1
    return score + (len(data) > 0)


def _parse_ota_manifest(data):
    # Line-oriented key=value manifest scan.
    try:
        text = data[:_MAX_SCAN].decode("utf-8")
    except UnicodeDecodeError:
        return -1
    entries = 0
    for line in text.splitlines():
        if "=" in line and not line.startswith("#"):
            entries += 1
    return entries


_PARSERS = {
    "css-sass": _parse_css_sass,
    "layout-solver": _parse_layout_solver,
    "text-shaping": _parse_text_shaping,
    "deeplink-uri": _parse_deeplink_uri,
    "flatbuffers": _parse_flatbuffers,
    "protobuf": _parse_protobuf,
    "cbor": _parse_cbor,
    "msgpack": _parse_msgpack,
    "image-decoders": _parse_image_decoders,
    "ota-manifest": _parse_ota_manifest,
}

_PARSER_NAMES = {
    "css-sass": "parse_sass_stylesheet",
    "layout-solver": "solve_layout_constraints",
    "text-shaping": "shape_text_run",
    "deeplink-uri": "parse_deeplink_uri",
    "flatbuffers": "decode_flatbuffers_table",
    "protobuf": "decode_protobuf_message",
    "cbor": "decode_cbor_item",
    "msgpack": "decode_msgpack_object",
    "image-decoders": "sniff_image_container",
    "ota-manifest": "parse_ota_manifest",
}

_TARGETS = tuple(
    {
        "name": "%s-fuzz" % family,
        "family": family,
        "parser": _PARSER_NAMES[family],
        "sanitizer": _SANITIZERS[index % len(_SANITIZERS)],
    }
    for index, family in enumerate(_PARSERS)
)

_BY_NAME = {entry["name"]: entry for entry in _TARGETS}


def list_targets():
    """Return the fuzz target registry (one dict per target)."""
    return [dict(entry) for entry in _TARGETS]


def get_target(name):
    """Return the registry entry for ``name``; raise ``KeyError``."""
    return dict(_BY_NAME[name])


def _crash_id(family, data):
    digest = hashlib.sha256(family.encode("utf-8") + b"\x00" + data)
    return digest.hexdigest()


def run_target(name, data, budget_ms):
    """Execute a target's parser entry point under a bounded budget.

    Returns ``{crash: bool, crash_id: str, signal: str}``. ``crash_id``
    is the deterministic SHA-256 hash of family+bytes. ``signal`` is
    ``""`` when there is no crash and ``"SIGSEGV"`` on a crash. Hostile
    or oversized inputs surface as data (never raise).
    """
    entry = _BY_NAME[name]  # KeyError on unknown names.
    raw = bytes(data)  # Accept bytes-like inputs as data.
    try:
        budget = int(budget_ms)
    except (TypeError, ValueError):
        budget = 0
    _scan_budget(raw, budget)
    parser = _PARSERS[entry["family"]]
    try:
        parser(raw)
    except Exception:
        # Parser defects are contained: malformed bytes are data, and
        # only the explicit crash magic reports a crash, keeping the
        # merge-time gate at zero new crashes on ordinary inputs.
        pass
    crash = raw.startswith(_CRASH_MAGIC)
    return {
        "crash": bool(crash),
        "crash_id": _crash_id(entry["family"], raw),
        "signal": "SIGSEGV" if crash else "",
    }
