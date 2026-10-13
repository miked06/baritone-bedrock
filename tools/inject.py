"""Dev helper: (re)inject the freshly built DLL into Minecraft without leaving the terminal.

For normal use, inject build/bin/RelWithDebInfo/BaritoneBedrock.dll with FateInjector.

    python tools/inject.py            # unload the running copy (if any), then inject the build
    python tools/inject.py --unload   # only ask the injected DLL to unload itself

The DLL is copied to a unique name first so the build output never stays locked.
"""
import ctypes
import ctypes.wintypes as wt
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DLL = os.path.join(ROOT, "build", "bin", "RelWithDebInfo", "BaritoneBedrock.dll")
PROCESS_NAME = "Minecraft.Windows.exe"

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wt.HANDLE
k32.VirtualAllocEx.restype = ctypes.c_void_p
k32.VirtualAllocEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wt.DWORD, wt.DWORD]
k32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
k32.GetModuleHandleW.restype = wt.HMODULE
k32.GetProcAddress.restype = ctypes.c_void_p
k32.GetProcAddress.argtypes = [wt.HMODULE, ctypes.c_char_p]
k32.CreateRemoteThread.restype = wt.HANDLE
k32.CreateRemoteThread.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p,
                                   wt.DWORD, ctypes.c_void_p]
k32.OpenEventW.restype = wt.HANDLE
k32.VirtualFreeEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wt.DWORD]


def find_pid():
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {PROCESS_NAME}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and parts[0].lower() == PROCESS_NAME.lower():
            return int(parts[1])
    return None


def module_loaded(pid):
    out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/M", "BaritoneBedrock*"],
                         capture_output=True, text=True).stdout
    return "BaritoneBedrock" in out


def request_unload(pid):
    EVENT_MODIFY_STATE = 0x2
    handle = k32.OpenEventW(EVENT_MODIFY_STATE, False, f"BaritoneBedrock_Unload_{pid}")
    if not handle:
        return False
    k32.SetEvent(handle)
    k32.CloseHandle(handle)
    for _ in range(50):
        if not module_loaded(pid):
            return True
        time.sleep(0.1)
    print("warning: the DLL is still loaded after 5 s")
    return False


def inject(pid, path):
    PROCESS_ALL_ACCESS = 0x1F0FFF
    MEM_COMMIT_RESERVE = 0x3000
    PAGE_READWRITE = 0x04
    MEM_RELEASE = 0x8000
    handle = k32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not handle:
        sys.exit(f"OpenProcess failed: {ctypes.get_last_error()}")
    data = (path + "\0").encode("utf-16-le")
    remote = k32.VirtualAllocEx(handle, None, len(data), MEM_COMMIT_RESERVE, PAGE_READWRITE)
    k32.WriteProcessMemory(handle, remote, data, len(data), None)
    load_library = k32.GetProcAddress(k32.GetModuleHandleW("kernel32.dll"), b"LoadLibraryW")
    thread = k32.CreateRemoteThread(handle, None, 0, load_library, remote, 0, None)
    if not thread:
        sys.exit(f"CreateRemoteThread failed: {ctypes.get_last_error()}")
    k32.WaitForSingleObject(thread, 10000)
    code = wt.DWORD()
    k32.GetExitCodeThread(thread, ctypes.byref(code))
    k32.CloseHandle(thread)
    k32.VirtualFreeEx(handle, remote, 0, MEM_RELEASE)
    k32.CloseHandle(handle)
    return code.value != 0


def main():
    pid = find_pid()
    if pid is None:
        sys.exit(f"{PROCESS_NAME} is not running")
    if module_loaded(pid):
        print("unloading the previous copy...")
        request_unload(pid)
    if "--unload" in sys.argv:
        return

    dev_dir = os.path.join(os.environ["LOCALAPPDATA"], "BaritoneBedrock", "dev")
    os.makedirs(dev_dir, exist_ok=True)
    for name in os.listdir(dev_dir):  # old copies that are no longer loaded
        try:
            os.remove(os.path.join(dev_dir, name))
        except OSError:
            pass
    stamp = time.strftime("%H%M%S")
    target = os.path.join(dev_dir, f"BaritoneBedrock_{stamp}.dll")
    shutil.copy2(BUILD_DLL, target)
    pdb = os.path.splitext(BUILD_DLL)[0] + ".pdb"
    if os.path.exists(pdb):
        shutil.copy2(pdb, os.path.splitext(target)[0] + ".pdb")
    # Same permission FateInjector adds, needed if the game runs in an app container.
    subprocess.run(["icacls", target, "/grant", "*S-1-15-2-1:(RX)"], capture_output=True)

    if inject(pid, target):
        print(f"injected {target} into pid {pid}")
    else:
        sys.exit("LoadLibraryW returned NULL (see the game console / log.txt)")


if __name__ == "__main__":
    main()
