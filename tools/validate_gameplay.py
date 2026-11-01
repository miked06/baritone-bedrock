"""Repeatable checks against an injected DLL in an existing Minecraft world.

Default checks only inspect status/inventory. A JSON scenario may issue normal
bot commands and assert log/status outcomes; actions require --allow-actions.
No world creation, game commands, teleportation or client-only inventory edits.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import time
from datetime import datetime, timezone

from cmd import find_pid


def exchange(pid, command, reply=False, timeout=3):
    name = rf"\\.\pipe\BaritoneBedrock_{pid}"
    deadline = time.monotonic() + timeout
    while True:
        try:
            pipe = open(name, "r+b" if reply else "wb", buffering=0)
            break
        except OSError:
            if time.monotonic() >= deadline:
                raise TimeoutError("Cannot connect to the Baritone dev pipe")
            ctypes.windll.kernel32.WaitNamedPipeW(name, 100)
            time.sleep(.02)
    with pipe:
        pipe.write(command.encode("utf-8"))
        if not reply:
            return ""
        import msvcrt
        handle = ctypes.c_void_p(msvcrt.get_osfhandle(pipe.fileno()))
        available = ctypes.c_ulong()
        while time.monotonic() < deadline:
            ok = ctypes.windll.kernel32.PeekNamedPipe(handle, None, 0, None, ctypes.byref(available), None)
            if not ok:
                raise OSError(ctypes.get_last_error(), "Status pipe disconnected")
            if available.value:
                return pipe.read(min(available.value, 16000)).decode("utf-8", errors="replace")
            time.sleep(.02)
        raise TimeoutError("Status request timed out")


def status(pid):
    return dict(line.split("=", 1) for line in exchange(pid, "@status", True).splitlines() if "=" in line)


def read_log(path, offset):
    if path.stat().st_size < offset:
        raise RuntimeError("The DLL log was reset during the scenario")
    with path.open("rb") as stream:
        stream.seek(offset)
        return stream.read(256 * 1024).decode("utf-8", errors="replace")


SMOKE = {
    "name": "Read-only module smoke checks",
    "steps": [
        {"name": "connection", "status": {"connection": "Connected"}},
        {"name": "inventory", "command": "#inv all", "expect": r"Command: inv all"},
        {"name": "survival", "command": "#survival", "expect": r"Survival assistant (enabled|disabled).*health"},
        {"name": "tasks", "command": "#task list", "expect": r"Command: task list"},
        {"name": "storage", "command": "#storage list", "expect": r"Command: storage list"},
        {"name": "portals", "command": "#portal list", "expect": r"Command: portal list"},
    ],
}


def validate_manifest(manifest):
    if not isinstance(manifest, dict) or not isinstance(manifest.get("steps"), list) or not 1 <= len(manifest["steps"]) <= 128:
        raise ValueError("A scenario needs 1..128 steps")
    for step in manifest["steps"]:
        if not isinstance(step, dict) or not isinstance(step.get("name"), str):
            raise ValueError("Each step needs a name")
        if not 0 < float(step.get("timeout", 5)) <= 300:
            raise ValueError("Step timeout must be 0..300 seconds")
        command = step.get("command", "")
        if not isinstance(command, str) or len(command.encode("utf-8")) > 4095 or "\n" in command or "\r" in command:
            raise ValueError("Invalid command")
        if command and not command.startswith("#"):
            raise ValueError("Only Baritone # commands are supported")
        if not command and "status" not in step:
            raise ValueError("Each step needs a command or status assertion")
        for key in ("expect", "reject"):
            if key in step:
                re.compile(step[key])
        if "status" in step and (not isinstance(step["status"], dict) or not step["status"]):
            raise ValueError("Status assertions must be a nonempty object")
    cleanup = manifest.get("cleanup", [])
    if not isinstance(cleanup, list) or len(cleanup) > 32 or any(not isinstance(c, str) or not c.startswith("#") or "\n" in c or "\r" in c or len(c.encode("utf-8"))>4095 for c in cleanup):
        raise ValueError("Invalid cleanup commands")


def run_step(pid, log, step):
    offset = log.stat().st_size
    start = time.monotonic()
    if step.get("command"):
        exchange(pid, step["command"])
    latest, current = "", {}
    while time.monotonic() - start < float(step.get("timeout", 5)):
        latest = read_log(log, offset)
        if ("[ERROR]" in latest or "[WARN ] [chat]" in latest) and not step.get("allow_error", False):
            raise RuntimeError(latest[-4000:])
        if "reject" in step and re.search(step["reject"], latest):
            raise RuntimeError("Rejected log pattern matched: " + latest[-4000:])
        if "status" in step:
            current = status(pid)
        matches_log = "expect" not in step or re.search(step["expect"], latest)
        matches_status = all(re.search(str(pattern), current.get(key, "")) for key, pattern in step.get("status", {}).items())
        if matches_log and matches_status and time.monotonic()-start >= .2:
            return {"name": step["name"], "passed": True, "seconds": round(time.monotonic()-start, 3), "log": latest[-4000:], "status": current}
        time.sleep(.05)
    raise TimeoutError(f"Step {step['name']} timed out. Status: {current}. Log: {latest[-4000:]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", type=Path)
    parser.add_argument("--allow-actions", action="store_true")
    parser.add_argument("--report", type=Path, default=Path("re/gameplay-report.json"))
    args = parser.parse_args()
    manifest = json.loads(args.scenario.read_text(encoding="utf-8")) if args.scenario else SMOKE
    validate_manifest(manifest)
    if args.scenario and not args.allow_actions:
        parser.error("Custom scenarios can change the world; use --allow-actions to run the reviewed commands")
    pid = find_pid()
    log = Path(os.environ["LOCALAPPDATA"]) / "BaritoneBedrock/log.txt"
    report = {"scenario": manifest.get("name", "Custom scenario"), "utc": datetime.now(timezone.utc).isoformat(), "pid": pid, "steps": [], "cleanup": []}
    initial = status(pid)
    if initial.get("connection") != "Connected" or not initial.get("world"):
        raise RuntimeError("Enter an existing world before running gameplay validation")
    report["initial_status"] = initial
    try:
        for step in manifest["steps"]:
            try:
                result = run_step(pid, log, step)
            except Exception as error:
                result = {"name": step["name"], "passed": False, "error": str(error)}
            report["steps"].append(result)
            print(("PASS " if result["passed"] else "FAIL ") + result["name"])
            if not result["passed"]:
                break
    finally:
        for command in manifest.get("cleanup", []):
            try:
                exchange(pid, command)
                report["cleanup"].append({"command": command, "submitted": True})
            except Exception as error:
                report["cleanup"].append({"command": command, "submitted": False, "error": str(error)})
        report["passed"] = len(report["steps"]) == len(manifest["steps"]) and all(s["passed"] for s in report["steps"]) and all(s["submitted"] for s in report["cleanup"])
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Report: {args.report.resolve()}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
