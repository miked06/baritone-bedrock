"""Verify fixed native profile references in an unmapped Minecraft image.

Use alongside sigcheck.py before enabling a package version. dump_module.py's
matching .base.txt sidecar is required because vtable pointers are relocated.
This is a read-only prerequisite check, not complete runtime certification.
"""
import argparse
import json
from pathlib import Path
import re
from relib import Image


def check_profile(dump, image_base=None):
    root = Path(__file__).resolve().parents[1]
    header = (root / 'src/sdk/Offsets.h').read_text(encoding='utf-8')
    values = {name: int(value, 16) for name, value in re.findall(r'inline constexpr uintptr_t\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+)', header)}
    base = image_base if image_base is not None else int(dump.with_suffix('.base.txt').read_text().strip(), 16)
    image = Image(dump)
    rows = []
    entries = [('CraftingController', 16, 'autoPlace'), ('CraftingController', 23, 'discard'),
               ('AnvilController', 8, 'preview'), ('AnvilController', 15, 'take'),
               ('GrindstoneController', 8, 'preview'), ('GrindstoneController', 15, 'take')]
    for controller, index, action in entries:
        table, expected = values[controller + '_vtable'], values[controller + '_' + action]
        actual = image.u64(table + index * 8) - base
        rows.append({'check': controller + '.' + action, 'valid': actual == expected and image.section_of(actual) == '.text',
                     'expected_rva': hex(expected), 'actual_rva': hex(actual)})
    prefix = bytes.fromhex('55 41 56 56 57 53 48 81 ec 90 00 00 00')
    rows.append({'check': 'inventory key handler', 'valid': image.bytes(values['ClientInstance_inventoryKey'], len(prefix)) == prefix})
    scene_prefix = bytes.fromhex('55 48 83 ec 70 48 8d 6c 24 70')
    rows.append({'check': 'scene stack pop', 'valid': image.bytes(values['SceneStack_pop'], len(scene_prefix)) == scene_prefix})
    empty = values['ItemStack_empty']
    rows.append({'check': 'canonical empty stack', 'valid': image.u64(empty) - base == values['ItemStack_vtable'] and
                 image.u64(empty + 8) == 0 and image.u64(empty + 16) == 0 and image.u8(empty + 0x22) == 0})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('--image-base', type=lambda value: int(value, 0))
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    try:
        rows = check_profile(args.dump, args.image_base)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    for row in rows:
        print(('OK   ' if row['valid'] else 'FAIL ') + row['check'])
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')
    print(f"{sum(row['valid'] for row in rows)}/{len(rows)} native profile checks passed")
    return 0 if rows and all(row['valid'] for row in rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
