#!/usr/bin/env python3
"""Recompress stored-deflate PNGs (from text_goldens_gen) with zlib level 9.
Reads each PNG, inflates the IDAT stream, re-deflates, rewrites the file.
Stdlib only. Usage: recompress_png.py <file.png> [...]"""
import struct
import sys
import zlib


def read_chunks(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "bad signature: " + path
    pos = 8
    ihdr = None
    idat = bytearray()
    others = []  # (type, payload) for non-IDAT/IEND chunks after IHDR
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        crc = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])[0]
        if zlib.crc32(ctype + payload) & 0xFFFFFFFF != crc:
            raise ValueError("bad CRC in %s chunk %r" % (path, ctype))
        if ctype == b"IHDR":
            ihdr = payload
        elif ctype == b"IDAT":
            idat += payload
        elif ctype == b"IEND":
            break
        else:
            others.append((ctype, payload))
        pos += 12 + length
    return ihdr, bytes(idat), others


def chunk(ctype, payload):
    out = struct.pack(">I", len(payload)) + ctype + payload
    out += struct.pack(">I", zlib.crc32(ctype + payload) & 0xFFFFFFFF)
    return out


def main(paths):
    for path in paths:
        ihdr, idat, others = read_chunks(path)
        raw = zlib.decompress(idat)
        red = zlib.compress(raw, 9)
        out = bytearray(b"\x89PNG\r\n\x1a\n")
        out += chunk(b"IHDR", ihdr)
        for ctype, payload in others:
            out += chunk(ctype, payload)
        # single IDAT is fine for these sizes
        out += chunk(b"IDAT", red)
        out += chunk(b"IEND", b"")
        before = __import__("os").path.getsize(path)
        with open(path, "wb") as f:
            f.write(out)
        # validate round-trip
        ihdr2, idat2, _ = read_chunks(path)
        assert zlib.decompress(idat2) == raw, "round-trip mismatch: " + path
        print("recompressed %s: %d -> %d bytes (pixels identical)" % (path, before, len(out)))


if __name__ == "__main__":
    main(sys.argv[1:])
