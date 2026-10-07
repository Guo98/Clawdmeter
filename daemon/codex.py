"""OpenAI Codex plan limits for the Clawdmeter Codex screen.

Codex CLI records the account's rate-limit state in every turn's
``token_count`` event in its session logs (~/.codex/sessions/**/rollout-*.jsonl):

    {"type": "event_msg", "payload": {"type": "token_count", "rate_limits": {
        "primary":   {"used_percent": 1.0, "window_minutes": 300,   "resets_at": <epoch>},
        "secondary": {"used_percent": 2.0, "window_minutes": 10080, "resets_at": <epoch>}}}}

Like the Claude side, this is a pure free-ride: it reads what Codex already
wrote and never touches Codex's credentials or calls OpenAI. The trade-off is
that the numbers only move when Codex is used on this machine; a window whose
reset time has passed is reported as 0% with an unknown reset.
"""

import json
import math
import os
import time
from pathlib import Path

TAIL_BYTES = 512 * 1024   # a turn's token_count lands near the end of a log
MAX_FILES = 8             # newest sessions to search before giving up


def _sessions_dir() -> Path:
    return Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "sessions"


def _latest_rate_limits() -> dict | None:
    root = _sessions_dir()
    if not root.is_dir():
        return None
    try:
        files = sorted(root.rglob("rollout-*.jsonl"),
                       key=lambda p: p.stat().st_mtime, reverse=True)[:MAX_FILES]
    except OSError:
        return None
    for path in files:
        try:
            with path.open("rb") as f:
                f.seek(0, os.SEEK_END)
                f.seek(max(0, f.tell() - TAIL_BYTES))
                lines = f.read().splitlines()
        except OSError:
            continue
        for raw in reversed(lines):
            if b'"rate_limits"' not in raw:
                continue
            try:
                payload = json.loads(raw).get("payload") or {}
            except ValueError:
                continue   # the first line of the tail may be cut mid-record
            limits = payload.get("rate_limits")
            if payload.get("type") == "token_count" and limits:
                return limits
    return None


def _window(w: dict | None, now: float) -> tuple[float, int]:
    if not w:
        return 0.0, -1
    resets_at = w.get("resets_at")
    if not resets_at or resets_at <= now:
        return 0.0, -1   # window rolled over since Codex last reported
    return round(float(w.get("used_percent") or 0.0), 1), math.ceil((resets_at - now) / 60)


def payload() -> dict | None:
    """The {"cx": {...}} device payload, or None if Codex has never reported."""
    limits = _latest_rate_limits()
    if limits is None:
        return None
    now = time.time()
    s, sr = _window(limits.get("primary"), now)
    w, wr = _window(limits.get("secondary"), now)
    return {"cx": {"s": s, "sr": sr, "w": w, "wr": wr}}


if __name__ == "__main__":
    print(json.dumps(payload(), indent=2))
