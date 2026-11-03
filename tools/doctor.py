"""Read-only Windows compatibility report; works before the DLL is injected.

Does not inject, update files in the game, change settings or scan game memory.
--report optionally writes a JSON report at the requested local path.
"""
import argparse
import ctypes
from ctypes import wintypes as wt
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess


def process_ids():
    result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Minecraft.Windows.exe', '/FO', 'CSV', '/NH'],
                            capture_output=True, text=True, check=True)
    return [int(row[1]) for row in csv.reader(result.stdout.splitlines())
            if len(row) > 1 and row[0].casefold() == 'minecraft.windows.exe']


def package_name(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    kernel.OpenProcess.restype = wt.HANDLE
    kernel.CloseHandle.argtypes = [wt.HANDLE]
    kernel.GetPackageFullName.argtypes = [wt.HANDLE, ctypes.POINTER(wt.UINT), wt.LPWSTR]
    kernel.GetPackageFullName.restype = wt.LONG
    handle = kernel.OpenProcess(0x1000, False, pid)  # Query only; no memory/write rights.
    if not handle:
        raise OSError(ctypes.get_last_error(), 'Could not query the Minecraft process')
    try:
        length = wt.UINT()
        result = kernel.GetPackageFullName(handle, ctypes.byref(length), None)
        if result != 122 or not 1 <= length.value <= 4096:
            raise OSError(result, 'Minecraft package identity unavailable')
        name = ctypes.create_unicode_buffer(length.value)
        result = kernel.GetPackageFullName(handle, ctypes.byref(length), name)
        if result:
            raise OSError(result, 'Could not read Minecraft package identity')
        return name.value
    finally:
        kernel.CloseHandle(handle)


def dll_details(path):
    with path.open('rb') as file:
        if file.read(2) != b'MZ':
            raise ValueError('DLL is not a PE file')
        file.seek(0x3c)
        offset = struct.unpack('<I', file.read(4))[0]
        if offset > path.stat().st_size - 26:
            raise ValueError('Invalid PE header bounds')
        file.seek(offset)
        header = file.read(26)
        if header[:4] != b'PE\0\0':
            raise ValueError('Invalid PE signature')
        machine = struct.unpack_from('<H', header, 4)[0]
        magic = struct.unpack_from('<H', header, 24)[0]
    digest = hashlib.sha256()
    with path.open('rb') as file:
        for data in iter(lambda: file.read(1024 * 1024), b''):
            digest.update(data)
    return {'x64': machine == 0x8664 and magic == 0x20b, 'sha256': digest.hexdigest(), 'bytes': path.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[1]
    parser.add_argument('--pid', type=int)
    parser.add_argument('--dll', type=Path, default=root / 'build/bin/RelWithDebInfo/BaritoneBedrock.dll')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if os.name != 'nt' or ctypes.sizeof(ctypes.c_void_p) != 8:
        parser.error('Run with 64-bit Python on Windows')
    header = (root / 'src/core/GameVersion.h').read_text(encoding='utf-8')
    match = re.search(r'package (\d+\.\d+\.\d+\.\d+)', header)
    if not match:
        parser.error('Cannot identify the project compatibility profile')
    report = {'supported_package': match.group(1), 'checks': [], 'read_only': True}
    def check(name, state, message):
        report['checks'].append({'check': name, 'state': state, 'message': message})
        print(f'{state.upper()} {name}: {message}')
    try:
        details = dll_details(args.dll)
        report['dll'] = details
        check('DLL architecture', 'pass' if details['x64'] else 'fail', 'Windows x64 PE' if details['x64'] else 'Expected an x64 Windows DLL')
    except (OSError, ValueError, struct.error) as error:
        check('DLL file', 'fail', str(error))
    pids = process_ids()
    selected = args.pid if args.pid in pids else pids[0] if len(pids) == 1 and args.pid is None else None
    if selected is None:
        check('Minecraft process', 'pending', 'Start Minecraft or select a running Minecraft PID explicitly with --pid')
    else:
        report['pid'] = selected
        try:
            identity = package_name(selected)
            report['package_identity'] = identity
            parts = identity.split('_')
            version = parts[1] if len(parts) > 1 else ''
            compatible = (version == match.group(1) and len(parts) > 2 and parts[0] == 'Microsoft.MinecraftUWP' and parts[2] == 'x64')
            check('Game package', 'pass' if compatible else 'fail', identity)
        except OSError as error:
            check('Game package', 'fail', str(error))
        try:
            from validate_gameplay import status
            live = status(selected)
            report['runtime'] = live
            check('Injected DLL', 'pass', live.get('connection', 'No connection state'))
            check('World', 'pass' if live.get('world') else 'pending', live.get('world') or 'Enter an existing world')
            policy = live.get('loadout_policy')
            check('Inventory policy', 'fail' if policy == 'Blocked' else 'pass' if policy == 'Ready' else 'pending', policy or 'Older DLL; load the current build for policy readiness')
        except (OSError, TimeoutError) as error:
            check('Injected DLL', 'pending', f'No responsive dev pipe: {error}')
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(f'Report: {args.report.resolve()}')
    return 1 if any(check['state'] == 'fail' for check in report['checks']) else 0


if __name__ == '__main__':
    raise SystemExit(main())
