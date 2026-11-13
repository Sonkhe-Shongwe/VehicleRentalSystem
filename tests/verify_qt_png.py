#!/usr/bin/env python3
"""Validate a real Qt screenshot without relying on PNG compression size.

Checks IHDR dimensions, chunk CRCs, stream integrity, scanline lengths and
basic content diversity. Does not claim a subjective visual/accessibility pass.
Standard-library only for Windows and Ubuntu GitHub runners.
"""
import pathlib
import struct
import sys
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def verify(path: pathlib.Path, expected_width: int, expected_height: int) -> None:
    data = path.read_bytes()
    if not data.startswith(SIGNATURE):
        raise ValueError("PNG signature missing")
    at = len(SIGNATURE)
    ihdr = None
    chunks = []
    finished = False
    while at + 12 <= len(data):
        length = struct.unpack_from(">I", data, at)[0]
        end = at + 12 + length
        if end > len(data):
            raise ValueError("Truncated PNG chunk")
        name = data[at + 4:at + 8]
        payload = data[at + 8:at + 8 + length]
        stored_crc = struct.unpack_from(">I", data, at + 8 + length)[0]
        actual_crc = zlib.crc32(name + payload) & 0xFFFFFFFF
        if stored_crc != actual_crc:
            raise ValueError(f"Corrupt PNG chunk {name!r}")
        if name == b"IHDR":
            if ihdr is not None or length != 13:
                raise ValueError("Duplicate or malformed PNG header")
            ihdr = struct.unpack(">IIBBBBB", payload)
        elif name == b"IDAT":
            chunks.append(payload)
        elif name == b"IEND":
            finished = True
            at = end
            break
        at = end
    if not finished or at != len(data) or not chunks or ihdr is None:
        raise ValueError("PNG missing IHDR/IDAT/IEND or has trailing data")

    width, height, depth, colour, compression, filtering, interlace = ihdr
    if (width, height) != (expected_width, expected_height):
        raise ValueError(f"Wrong screenshot dimensions: {width} x {height}")
    if depth != 8 or colour not in CHANNELS or (compression, filtering, interlace) != (0, 0, 0):
        raise ValueError(f"Unsupported PNG layout: {ihdr}")
    if width * height > 20_000_000:
        raise ValueError("Unexpectedly large screenshot")
    scanline = (width * depth * CHANNELS[colour] + 7) // 8
    raw = zlib.decompress(b"".join(chunks))
    if len(raw) != (scanline + 1) * height:
        raise ValueError("PNG scanline data is incomplete")
    distinct = len(set(raw))
    if distinct < 16:
        raise ValueError(f"Screenshot lacks visual information ({distinct} byte values)")
    print(f"PASS {path.name}: {width} x {height}, PNG checksums and image content valid")


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: verify_qt_png.py screenshot.png width height")
    verify(pathlib.Path(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]))


if __name__ == "__main__":
    main()
