#!/usr/bin/env python3
"""Verify a Nioh executable without modifying it."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

SUPPORTED_TIMESTAMP = 0x6307ABD5
SUPPORTED_IMAGE_SIZE = 0x0306E000
SUPPORTED_SHA256 = "56006af3fc0945248aa7a2e33fd95d4e510f1dbe3395eb3644dae3c2806377f6"
PROFILE_SIGNATURE = bytes.fromhex(
    "000070420100000001000000"
    "0000f0410100000002000000"
    "0000f0410100000002000000"
    "000070420100000002000000"
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()

    data = args.executable.read_bytes()
    if data[:2] != b"MZ":
        raise SystemExit("not a PE executable")

    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise SystemExit("invalid PE signature")

    timestamp = struct.unpack_from("<I", data, pe_offset + 8)[0]
    optional_header = pe_offset + 24
    image_size = struct.unpack_from("<I", data, optional_header + 56)[0]
    matches = data.count(PROFILE_SIGNATURE)
    digest = hashlib.sha256(data).hexdigest()

    print(f"path: {args.executable}")
    print(f"sha256: {digest}")
    print(f"timestamp: 0x{timestamp:08X}")
    print(f"image size: 0x{image_size:08X}")
    print(f"frame profile matches: {matches}")

    validated = (
        timestamp == SUPPORTED_TIMESTAMP
        and image_size == SUPPORTED_IMAGE_SIZE
        and matches == 1
        and digest == SUPPORTED_SHA256
    )
    print(f"validated build metadata: {'yes' if validated else 'no'}")
    if not validated:
        print("runtime compatibility requires the complete in-memory signature scan")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
