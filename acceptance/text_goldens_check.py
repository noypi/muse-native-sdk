#!/usr/bin/env python3
"""T8 acceptance gate — text golden samples (AC-TEXT-046).

Regenerates the 12 Latin/CJK/emoji/small-size samples at scales 1.0/1.3/2.0
through the engine stack (via text_goldens_gen --samples) and compares
DECODED PIXELS against the committed goldens under
examples/m3-gallery/build/goldens/.

Exit 0 iff every sample matches within the recorded tolerance, nonzero with
a per-sample delta report otherwise (never a silent pass).

Usage:
  text_goldens_check.py --gen <path-to-text_goldens_gen> --goldens <dir>
                        [--scripts latin,cjk,emoji,small] [--scales 1.0,1.3,2.0]

Tolerance (recorded, not relaxed to fit): exact pixel match (perPixel=0,
maxFailPct=0.0). The generator is fully deterministic (fixed advances,
placements, coverage; no wall-clock, no RNG), so any byte drift is a real
stack change. Page-level goldens keep their own m3-tolerances.json.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

SAMPLES = ("latin", "cjk", "emoji", "small")
SCALES = ("1", "1.3", "2")


def decode_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: bad signature" % path)
    pos = 8
    width = height = ctype = bitd = None
    parts = []
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype4 = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        if ctype4 == b"IHDR":
            width, height, bitd, ctype = struct.unpack(">IIBB", payload[:10])
        elif ctype4 == b"IDAT":
            parts.append(payload)
        elif ctype4 == b"IEND":
            break
        pos += 12 + length
    if width is None or bitd != 8 or ctype != 2:
        raise ValueError("%s: expected 8-bit RGB" % path)
    raw = zlib.decompress(b"".join(parts))
    channels = 3
    stride = width * channels
    if len(raw) != height * (stride + 1):
        raise ValueError("%s: short pixel buffer" % path)
    # Strip per-scanline filter bytes (generator always emits filter 0).
    out = bytearray(height * stride)
    for y in range(height):
        if raw[y * (stride + 1)] != 0:
            raise ValueError("%s: unsupported filter on row %d" % (path, y))
        out[y * stride:(y + 1) * stride] = raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)]
    return width, height, bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", required=True)
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--scripts", default=",".join(SAMPLES))
    ap.add_argument("--scales", default=",".join(SCALES))
    args = ap.parse_args()
    scripts = [s for s in args.scripts.split(",") if s]
    scales = [s for s in args.scales.split(",") if s]
    want = []
    for s in scripts:
        for sc in scales:
            want.append("text-%s-%s.png" % (s, sc))
    with tempfile.TemporaryDirectory(prefix="text-goldens-") as tmp:
        r = subprocess.run([args.gen, "--samples", tmp],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print("FAIL: generator exited %d\n%s%s" % (r.returncode, r.stdout, r.stderr))
            return 1
        failures = 0
        for name in want:
            fresh = os.path.join(tmp, name)
            committed = os.path.join(args.goldens, name)
            if not os.path.isfile(committed):
                print("FAIL %s: missing committed golden" % name)
                failures += 1
                continue
            try:
                fw, fh, fpix = decode_png(fresh)
                cw, ch, cpix = decode_png(committed)
            except ValueError as e:
                print("FAIL %s: %s" % (name, e))
                failures += 1
                continue
            if (fw, fh) != (cw, ch):
                print("FAIL %s: geometry %dx%d != committed %dx%d"
                      % (name, fw, fh, cw, ch))
                failures += 1
                continue
            diff = sum(1 for a, b in zip(fpix, cpix) if a != b)
            if diff:
                print("FAIL %s: %d/%d bytes differ (tolerance: exact match)"
                      % (name, diff, len(fpix)))
                failures += 1
            else:
                print("ok %s (%dx%d, %d bytes identical)" % (name, fw, fh, len(fpix)))
        if failures:
            print("gallery-text-goldens: %d FAILURE(S)" % failures)
            return 1
        print("gallery-text-goldens: all %d samples within recorded tolerance (exact)" % len(want))
        return 0


if __name__ == "__main__":
    sys.exit(main())
