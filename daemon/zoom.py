"""Zoom meeting controls for the Clawdmeter Zoom screen.

Drives the Zoom desktop app with System Events UI scripting, so it works
without bringing Zoom to the front:

- State and toggles use Zoom's "Meeting" menu (Mute/Unmute audio,
  Start/Stop video). The menu is always reachable, unlike the meeting
  window, which drops out of the accessibility tree when it's on another
  Space or full screen, or while Zoom auto-hides its controls.
- Zoom relabels those items 1-4 s after a toggle, so after each of our own
  clicks the expected state is held until Zoom agrees (or a grace period
  lapses) instead of bouncing back to the old label.

UI scripting needs the macOS Accessibility permission for whatever runs
this (the daemon's Python under launchd); without it every call reports
``ZOOM_NO_ACCESS`` so the device can say so.
"""

import json
import os
import re
import subprocess
import sys
import time

ZOOM_OFF, ZOOM_IDLE, ZOOM_MEETING, ZOOM_NO_ACCESS = 0, 1, 2, 3

CMD_MIC, CMD_VIDEO = 0x10, 0x11

PROCESS = "zoom.us"

# One line per fact: "B<TAB>description" for each meeting-window button,
# "M<TAB>menu<TAB>item" for each menu item. "!NOPROC" if Zoom isn't running.
_SNAPSHOT = f'''
tell application "System Events"
    if not (exists process "{PROCESS}") then return "!NOPROC"
    set out to {{}}
    tell process "{PROCESS}"
        repeat with w in (windows whose name contains "Meeting")
            try
                set ds to description of every button of w
                repeat with i from 1 to count of ds
                    set d to item i of ds
                    if d is not missing value then set end of out to "B" & tab & d
                end repeat
            end try
        end repeat
        repeat with mb in menu bar items of menu bar 1
            try
                set mn to name of mb
                if mn is not "Apple" then
                    set nms to name of every menu item of menu 1 of mb
                    repeat with i from 1 to count of nms
                        set nm to item i of nms
                        if nm is not missing value then set end of out to "M" & tab & mn & tab & nm
                    end repeat
                end if
            end try
        end repeat
    end tell
    set AppleScript's text item delimiters to linefeed
    return out as text
end tell
'''

_CLICK_MENU = f'''
on run argv
    tell application "System Events" to tell process "{PROCESS}"
        click menu item (item 2 of argv) of menu 1 of menu bar item (item 1 of argv) of menu bar 1
    end tell
end run
'''

_DENIED = re.compile(r"assistive access|not authori[sz]ed|-25211|-1743", re.I)

_UNMUTE_BTN = re.compile(r"^unmute( my)? audio", re.I)
_MUTE_BTN = re.compile(r"^mute( my)? audio", re.I)
_START_VIDEO_BTN = re.compile(r"^start( my)? video", re.I)
_STOP_VIDEO_BTN = re.compile(r"^stop( my)? video", re.I)
_MUTE_MENU = re.compile(r"^(un)?mute audio$", re.I)
_VIDEO_MENU = re.compile(r"^(start|stop) video$", re.I)


class NoAccess(Exception):
    pass


class _Snapshot:
    def __init__(self, raw: str) -> None:
        self.buttons: list[str] = []
        self.menus: list[tuple[str, str]] = []
        for line in raw.splitlines():
            kind, _, rest = line.partition("\t")
            if kind == "B":
                self.buttons.append(rest.strip())
            elif kind == "M":
                menu, sep, item = rest.partition("\t")
                if sep:
                    self.menus.append((menu, item.strip()))

    def button(self, *patterns: re.Pattern) -> str | None:
        return next((b for b in self.buttons for p in patterns if p.match(b)), None)

    def menu(self, pattern: re.Pattern) -> tuple[str, str] | None:
        return next(((m, i) for m, i in self.menus if pattern.match(i)), None)

    @property
    def in_meeting(self) -> bool:
        return bool(self.menu(_MUTE_MENU) or self.menu(_VIDEO_MENU)
                    or self.button(_MUTE_BTN, _UNMUTE_BTN))


# Last state read from Zoom, carried across polls where it can't be read.
_cache = {"muted": False, "video": False}
# Zoom relabels its controls 1-4 s after a toggle, so a read right after our
# own click still shows the old state. Each click records the state it should
# produce; contrary reads are ignored until Zoom agrees or this lapses.
EXPECT_GRACE = 6.0
_expect: dict[str, tuple[bool, float]] = {}


def permission_target() -> str:
    """What to add under Privacy & Security > Accessibility: the running
    executable's enclosing .app (Homebrew/framework Pythons re-exec into
    Python.app, and that bundle is what macOS checks)."""
    exe = subprocess.run(["ps", "-o", "comm=", "-p", str(os.getpid())],
                         capture_output=True, text=True).stdout.strip() or sys.executable
    i = exe.find(".app/")
    return exe[: i + 4] if i >= 0 else exe


def available() -> bool:
    return sys.platform == "darwin"


def _running() -> bool:
    return subprocess.run(["pgrep", "-xq", PROCESS]).returncode == 0


def _osascript(script: str, *args: str) -> str:
    res = subprocess.run(["osascript", "-e", script, *args],
                         capture_output=True, text=True, timeout=15)
    if res.returncode != 0:
        if _DENIED.search(res.stderr):
            raise NoAccess(res.stderr.strip())
        raise RuntimeError(res.stderr.strip()[:200] or f"osascript exited {res.returncode}")
    return res.stdout.rstrip("\n")


def _snapshot() -> _Snapshot | None:
    raw = _osascript(_SNAPSHOT)
    return None if raw == "!NOPROC" else _Snapshot(raw)


def _observe(key: str, seen: bool | None) -> None:
    if seen is None:          # nothing readable: keep what we had
        return
    want = _expect.get(key)
    if want is not None:
        value, deadline = want
        if seen != value and time.monotonic() < deadline:
            return            # Zoom hasn't relabeled yet
        del _expect[key]
    _cache[key] = seen


def _read_state(snap: _Snapshot) -> None:
    def seen(on: re.Pattern, off: re.Pattern) -> bool | None:
        if snap.menu(on) or snap.button(on):
            return True
        if snap.menu(off) or snap.button(off):
            return False
        return None
    _observe("muted", seen(_UNMUTE_BTN, _MUTE_BTN))
    _observe("video", seen(_STOP_VIDEO_BTN, _START_VIDEO_BTN))


def payload() -> dict:
    """The {"zm": {...}} device payload for Zoom's current state."""
    if not _running():
        return {"zm": {"s": ZOOM_OFF, "a": 0, "v": 0}}
    try:
        snap = _snapshot()
    except NoAccess:
        return {"zm": {"s": ZOOM_NO_ACCESS, "a": 0, "v": 0}}
    if snap is None:
        return {"zm": {"s": ZOOM_OFF, "a": 0, "v": 0}}
    if not snap.in_meeting:
        return {"zm": {"s": ZOOM_IDLE, "a": 0, "v": 0}}
    _read_state(snap)
    return {"zm": {"s": ZOOM_MEETING, "a": int(_cache["muted"]), "v": int(_cache["video"])}}


def _toggle(snap: _Snapshot, key: str, menu: re.Pattern) -> None:
    _read_state(snap)
    item = snap.menu(menu)
    if item is None:
        raise RuntimeError("Zoom meeting menu not found")
    _osascript(_CLICK_MENU, *item)
    # Hold the flipped state until Zoom relabels the item.
    _cache[key] = not _cache[key]
    _expect[key] = (_cache[key], time.monotonic() + EXPECT_GRACE)


def command(cmd: int) -> str | None:
    """Run a device command against Zoom. Returns a note worth logging, if any.
    Raises NoAccess / RuntimeError."""
    if not _running():
        return None
    snap = _snapshot()
    if snap is None or not snap.in_meeting:
        return "not in a meeting"
    if cmd == CMD_MIC:
        _toggle(snap, "muted", _MUTE_MENU)
    elif cmd == CMD_VIDEO:
        _toggle(snap, "video", _VIDEO_MENU)
    return None


if __name__ == "__main__":
    if sys.argv[1:] == ["snapshot"]:
        snap = _snapshot()
        for b in (snap.buttons if snap else []):
            print(f"button\t{b}")
        for m, i in (snap.menus if snap else []):
            print(f"menu\t{m}\t{i}")
    else:
        print(json.dumps(payload()))
