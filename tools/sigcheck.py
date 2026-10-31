"""Check every C++ signature against an unmapped dump, without injecting the DLL.

Usage: python tools/sigcheck.py re/Minecraft.Windows_<version>.exe [--json report.json]
Requires numpy (like relib.py). A missing/ambiguous pattern is a failure.
"""
import argparse
import json
from pathlib import Path
import re

from relib import Image


def signatures():
    header = Path(__file__).resolve().parents[1] / "src/memory/Signatures.h"
    pattern = r'inline Signature\s+(\w+)\s*\{\s*"([^"]+)"\s*,\s*"([^"]+)"(?:\s*,\s*Resolve::(\w+)\s*,\s*(\d+))?\s*\};'
    return re.findall(pattern, header.read_text(encoding="utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    image = Image(args.dump)
    rows = []
    for key, name, pattern, mode, offset in signatures():
        matches = image.scan(pattern, limit=100)
        target = None
        if len(matches) == 1:
            target = image.rip_target(matches[0], int(offset)) if mode == "Rel32" else matches[0]
        valid = len(matches) == 1 and image.section_of(target) is not None
        rows.append(dict(key=key, name=name, matches=matches, target=target, valid=valid))
        detail = f"0x{target:X} ({image.section_of(target)})" if target is not None else f"{len(matches)} matches"
        print(f"{'OK' if valid else 'FAIL':4} {key:38} {detail}")
    if args.json:
        args.json.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    print(f"{sum(r['valid'] for r in rows)}/{len(rows)} unique signatures with targets inside the image")
    return 0 if rows and all(r["valid"] for r in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
