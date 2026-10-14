#!/usr/bin/env python3
"""Extract a window of dumpbin /disasm output by virtual address.

Usage: dis.py <lo-hex> [hi-hex] [file]
Addresses are virtual (image base 0x6F000000 + RVA).
"""
import re
import sys

ADDR = re.compile(r"^\s*([0-9A-F]{8}):")


def main(argv):
    lo = int(argv[0], 16)
    hi = int(argv[1], 16) if len(argv) > 1 and not argv[1].endswith(".txt") else lo + 0x200
    path = argv[-1] if argv[-1].endswith(".txt") else "game127.disasm.txt"
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = ADDR.match(line)
            if not match:
                continue
            va = int(match.group(1), 16)
            if lo <= va <= hi:
                print(line.rstrip())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
