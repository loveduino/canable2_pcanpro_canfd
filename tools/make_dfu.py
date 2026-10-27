#!/usr/bin/env python3
"""
Package a compiled .bin firmware into an STM32 DfuSe-standard .dfu file.

Usage:
    python tools/make_dfu.py [input.bin] [output.dfu]

Defaults:
    input  = the most recently modified *.bin found in build/ (falls back to a
             build/canable2_*.bin glob if no explicit path is given)
    output = same base name as input, with the extension changed to .dfu

Notes:
    STM32CubeProgrammer / dfu-util can both flash .hex / .bin directly and do
    NOT require the .dfu format; this script is only needed when a DfuSe file
    is explicitly required.

    DfuSe file layout:
        [DFU prefix 11B] + [target prefix 274B] + [firmware data] + [DFU suffix 16B]
"""
import glob
import os
import struct
import sys

FLASH_BASE = 0x08000000


def crc32_mpeg2(data: bytes) -> int:
    """ST DfuSe uses CRC-32/MPEG-2 (poly 0x04C11DB7, init 0xFFFFFFFF, no final xor)."""
    c = 0xFFFFFFFF
    for byte in data:
        c ^= byte
        for _ in range(8):
            c = (c >> 1) ^ (0xEDB88320 if c & 1 else 0)
    return c & 0xFFFFFFFF


def make_dfu(bin_path: str, out_path: str, vid: int = 0x0483, pid: int = 0xDF11) -> None:
    with open(bin_path, "rb") as f:
        data = f.read()

    # 1) DFU prefix (11 bytes)
    prefix = b"DfuSe" + bytes([0x01]) + struct.pack("<I", len(data)) + bytes([0x01])

    # 2) target prefix (274 bytes)
    target = b"Target" + bytes([0x00, 0x01])
    target += struct.pack("<I", FLASH_BASE)   # element address
    target += struct.pack("<I", len(data))    # element size
    target += b"\x00" * (274 - len(target))

    # 3) DFU suffix (16 bytes)
    #    bcdDevice(2) + idProduct(2) + idVendor(2) + bcdDFU(2) + 'UFD'(3) + bLength(1) + dwCRC(4)
    suffix_header = struct.pack("<HHHH", 0x0000, pid, vid, 0x011A) + b"UFD" + bytes([16])
    crc = crc32_mpeg2(prefix + target + data + suffix_header)
    suffix = suffix_header + struct.pack("<I", crc)

    dfu = prefix + target + data + suffix
    with open(out_path, "wb") as f:
        f.write(dfu)

    print(f"[OK] {out_path}")
    print(f"     bin size: {len(data)} bytes")
    print(f"     dfu size: {len(dfu)} bytes")
    print(f"     CRC32:    0x{crc:08X}")


def find_latest_bin(build_dir: str = "build") -> str | None:
    """Return the most recently modified *.bin under build_dir, or None if none."""
    pattern = os.path.join(build_dir, "*.bin")
    candidates = glob.glob(pattern)
    if not candidates:
        return None
    return max(candidates, key=os.path.getmtime)


def main() -> None:
    bin_path = sys.argv[1] if len(sys.argv) > 1 else find_latest_bin()
    if bin_path is None:
        print("[ERROR] No input file given and no .bin found in build/.", file=sys.stderr)
        print("        Usage: python tools/make_dfu.py [input.bin] [output.dfu]", file=sys.stderr)
        sys.exit(1)

    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(bin_path)[0] + ".dfu"

    if not os.path.exists(bin_path):
        print(f"[ERROR] Input file not found: {bin_path}", file=sys.stderr)
        sys.exit(1)

    make_dfu(bin_path, out_path)


if __name__ == "__main__":
    main()
