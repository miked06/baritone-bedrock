"""Dump Minecraft.Windows.exe from the memory of the running game.

The executable in WindowsApps cannot be read from disk, but the running image can.
The dump is "unmapped" (raw offsets = virtual addresses) so IDA / Ghidra / the other
scripts of this folder can open it directly.

Usage: python tools/dump_module.py [output_dir]
"""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import sys

PROCESS_NAME = "Minecraft.Windows.exe"
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
TH32CS_SNAPPROCESS = 0x2
TH32CS_SNAPMODULE = 0x8
TH32CS_SNAPMODULE32 = 0x10
MEM_COMMIT = 0x1000
PAGE_NOACCESS = 0x01
PAGE_GUARD = 0x100

k32 = ctypes.WinDLL("kernel32", use_last_error=True)


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wt.DWORD),
                ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD),
                ("pcPriClassBase", ctypes.c_long), ("dwFlags", wt.DWORD),
                ("szExeFile", ctypes.c_wchar * 260)]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("GlblcntUsage", wt.DWORD), ("ProccntUsage", wt.DWORD),
                ("modBaseAddr", ctypes.c_void_p), ("modBaseSize", wt.DWORD),
                ("hModule", wt.HMODULE), ("szModule", ctypes.c_wchar * 256),
                ("szExePath", ctypes.c_wchar * 260)]


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wt.DWORD), ("PartitionId", wt.WORD),
                ("RegionSize", ctypes.c_size_t), ("State", wt.DWORD),
                ("Protect", wt.DWORD), ("Type", wt.DWORD)]


k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.OpenProcess.restype = wt.HANDLE
k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(MEMORY_BASIC_INFORMATION), ctypes.c_size_t]
k32.VirtualQueryEx.restype = ctypes.c_size_t
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]


def find_pid(name):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(entry)
    ok = k32.Process32FirstW(snap, ctypes.byref(entry))
    while ok:
        if entry.szExeFile.lower() == name.lower():
            k32.CloseHandle(snap)
            return entry.th32ProcessID
        ok = k32.Process32NextW(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return None


def find_module(pid, name):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    entry = MODULEENTRY32W()
    entry.dwSize = ctypes.sizeof(entry)
    ok = k32.Module32FirstW(snap, ctypes.byref(entry))
    while ok:
        if entry.szModule.lower() == name.lower():
            k32.CloseHandle(snap)
            return entry.modBaseAddr, entry.modBaseSize, entry.szExePath
        ok = k32.Module32NextW(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return None


def read_image(handle, base, size):
    image = bytearray(size)
    addr = base
    end = base + size
    mbi = MEMORY_BASIC_INFORMATION()
    unreadable = 0
    while addr < end:
        if not k32.VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
            break
        region_end = min(mbi.BaseAddress + mbi.RegionSize, end)
        readable = mbi.State == MEM_COMMIT and not (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        if readable:
            length = region_end - addr
            buf = ctypes.create_string_buffer(length)
            got = ctypes.c_size_t(0)
            if k32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, length, ctypes.byref(got)):
                image[addr - base:addr - base + got.value] = buf.raw[:got.value]
            else:
                unreadable += length
        else:
            unreadable += region_end - addr
        addr = region_end
    return image, unreadable


def unmap_sections(image):
    """Make raw offsets equal to RVAs so the dump loads as a normal PE file."""
    e_lfanew = struct.unpack_from("<I", image, 0x3C)[0]
    num_sections = struct.unpack_from("<H", image, e_lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", image, e_lfanew + 20)[0]
    sec = e_lfanew + 24 + opt_size
    for i in range(num_sections):
        off = sec + i * 40
        vsize, vaddr = struct.unpack_from("<II", image, off + 8)
        struct.pack_into("<II", image, off + 16, vsize, vaddr)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "re")
    pid = find_pid(PROCESS_NAME)
    if pid is None:
        sys.exit(f"{PROCESS_NAME} is not running")
    mod = find_module(pid, PROCESS_NAME)
    if mod is None:
        sys.exit("module not found")
    base, size, path = mod
    print(f"pid={pid} base=0x{base:X} size=0x{size:X}\n{path}")

    handle = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not handle:
        sys.exit(f"OpenProcess failed: {ctypes.get_last_error()}")
    image, unreadable = read_image(handle, base, size)
    k32.CloseHandle(handle)
    unmap_sections(image)

    # Package folder name holds the version, e.g. Microsoft.MinecraftUWP_1.26.5203.0_x64__...
    version = os.path.basename(os.path.dirname(path)).split("_")[1] if "_" in path else "unknown"
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"Minecraft.Windows_{version}.exe")
    with open(out, "wb") as f:
        f.write(image)
    with open(os.path.join(out_dir, f"Minecraft.Windows_{version}.base.txt"), "w") as f:
        f.write(f"0x{base:X}\n")
    print(f"wrote {out} ({len(image) / 1e6:.1f} MB, {unreadable / 1e6:.1f} MB unreadable)")


if __name__ == "__main__":
    main()
