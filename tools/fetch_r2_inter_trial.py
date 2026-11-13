#!/usr/bin/env python3
"""Fetch and verify one official, commit-pinned Inter TTF for opt-in Qt builds.

This build-only helper never downloads media or runs in the application.
Validation uses the SHA-1 of Git's "blob <length>\\0" + exact TTF bytes.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import tempfile
from urllib.request import Request, urlopen

COMMIT = "38411257417e616c5d4daa1b9e91a3dbcebe1c2e"
URL = (
    "https://raw.githubusercontent.com/google/fonts/"
    + COMMIT
    + "/ofl/inter/Inter%5Bopsz%2Cwght%5D.ttf"
)
BYTES = 876576
GIT_BLOB_SHA1 = "047c92f6e2212473dc436020afed689527076d44"


def is_verified(data: bytes) -> bool:
    if len(data) != BYTES:
        return False
    prefix = b"blob " + str(len(data)).encode("ascii") + b"\0"
    return hashlib.sha1(prefix + data).hexdigest() == GIT_BLOB_SHA1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    out = Path(args.output)
    if out.is_file() and is_verified(out.read_bytes()):
        print("Inter trial font: verified cached official source")
        return 0
    request = Request(URL, headers={"User-Agent": "RentalStudio-R2-opt-in-font-trial"})
    with urlopen(request, timeout=45) as response:
        data = response.read(BYTES + 1)
    if not is_verified(data):
        raise ValueError("Inter font bytes do not match the pinned official Git blob")
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        dir=out.parent, prefix="inter-verified-", delete=False
    ) as temp:
        staged = Path(temp.name)
        temp.write(data)
    try:
        os.replace(staged, out)
    finally:
        staged.unlink(missing_ok=True)
    print("Inter trial font: pinned official bytes verified and staged for Qt resource")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
