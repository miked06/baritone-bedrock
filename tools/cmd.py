"""Dev helper: run bot commands from a terminal, as if typed in the game chat.

    python tools/cmd.py "#debug"
    python tools/cmd.py "#goto 100 64 100" --wait 5   # then print the log lines written in 5 s

Commands run on the next game tick (you must be in a world). Output goes to the chat
and to %LOCALAPPDATA%/BaritoneBedrock/log.txt.
"""
import os
import subprocess
import sys
import time

LOG = os.path.join(os.environ["LOCALAPPDATA"], "BaritoneBedrock", "log.txt")


def find_pid():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Minecraft.Windows.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and parts[0].lower() == "minecraft.windows.exe":
            return int(parts[1])
    sys.exit("Minecraft is not running")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    wait = 2.0
    if "--wait" in sys.argv:
        wait = float(sys.argv[sys.argv.index("--wait") + 1])
        args = [a for a in args if a != sys.argv[sys.argv.index("--wait") + 1]]
    if not args:
        sys.exit(__doc__)
    pid = find_pid()
    start = os.path.getsize(LOG) if os.path.exists(LOG) else 0
    with open(rf"\\.\pipe\BaritoneBedrock_{pid}", "wb", buffering=0) as pipe:
        for line in args:
            pipe.write(line.encode("utf-8"))
    time.sleep(wait)
    with open(LOG, "rb") as f:
        f.seek(start)
        sys.stdout.write(f.read().decode("utf-8", errors="replace"))


if __name__ == "__main__":
    main()
