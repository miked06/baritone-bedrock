"""Verify fixed native profile references in an unmapped Minecraft image.

Use alongside sigcheck.py before enabling a package version. dump_module.py's
matching .base.txt sidecar is required because vtable pointers are relocated.
This is a read-only prerequisite check, not complete runtime certification.
"""
import argparse
import json
from pathlib import Path
import re


def native_profiles():
    root = Path(__file__).resolve().parents[1]
    source = (root / 'src/core/NativeProfiles.inc').read_text(encoding='utf-8')
    entries = re.findall(r'^PROFILE\("(\d+\.\d+\.\d+\.\d+)",\s*([^)]*)\)', source, re.M)
    fields = ('craftingTable', 'craft', 'discard', 'anvilTable', 'anvilPreview', 'anvilTake',
              'grindstoneTable', 'grindstonePreview', 'grindstoneTake',
              'inventoryKey', 'scenePop', 'emptyStack', 'itemStackTable')
    profiles = {version: dict(zip(fields, (int(value.strip(), 16) for value in addresses.split(',')), strict=True))
                for version, addresses in entries}
    if not profiles or len(profiles) != len(entries):
        raise ValueError('Missing or duplicate native package profile')
    return profiles


def check_profile(dump, image_base=None, package=None):
    from relib import Image

    profiles = native_profiles()
    if package is None:
        match = re.fullmatch(r'Minecraft\.Windows_(\d+\.\d+\.\d+\.\d+)\.exe', dump.name)
        if not match:
            raise ValueError('Pass --package when the dump filename does not include the package version')
        package = match.group(1)
    if package not in profiles:
        raise ValueError(f'No native profile for {package}. Available: {", ".join(profiles)}')
    values = profiles[package]
    base = image_base if image_base is not None else int(dump.with_suffix('.base.txt').read_text().strip(), 16)
    image = Image(dump)
    rows = []
    entries = [('craftingTable', 16, 'craft'), ('craftingTable', 23, 'discard'),
               ('anvilTable', 8, 'anvilPreview'), ('anvilTable', 15, 'anvilTake'),
               ('grindstoneTable', 8, 'grindstonePreview'), ('grindstoneTable', 15, 'grindstoneTake')]
    for controller, index, action in entries:
        table, expected = values[controller], values[action]
        actual = image.u64(table + index * 8) - base
        rows.append({'check': action, 'valid': actual == expected and image.section_of(actual) == '.text',
                     'expected_rva': hex(expected), 'actual_rva': hex(actual)})
    prefix = bytes.fromhex('55 41 56 56 57 53 48 81 ec 90 00 00 00')
    rows.append({'check': 'inventory key handler', 'valid': image.bytes(values['inventoryKey'], len(prefix)) == prefix})
    scene_prefix = bytes.fromhex('55 48 83 ec 70 48 8d 6c 24 70')
    rows.append({'check': 'scene stack pop', 'valid': image.bytes(values['scenePop'], len(scene_prefix)) == scene_prefix})
    empty = values['emptyStack']
    rows.append({'check': 'canonical empty stack', 'valid': image.u64(empty) - base == values['itemStackTable'] and
                 image.u64(empty + 8) == 0 and image.u64(empty + 16) == 0 and image.u8(empty + 0x22) == 0})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('--package', help='Override the package version inferred from the dump filename')
    parser.add_argument('--image-base', type=lambda value: int(value, 0))
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    try:
        rows = check_profile(args.dump, args.image_base, args.package)
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
