"""GitHub PRs for the Clawdmeter PRs screen.

Lists open, non-draft pull requests that currently request the signed-in
user's review (directly or via a team), most recently updated first. A PR
drops off once the user reviews it, the request is removed, or it closes.
Everything goes through the `gh` CLI, so it reuses whatever account
`gh auth login` set up and never handles a token itself.

The list is bigger than one BLE write (the firmware RX buffer is 512 bytes),
so it goes out as a few {"gh":{...,"o":offset,"t":rows}} chunks that the
device reassembles.
"""

import json
import shutil
import subprocess
import unicodedata
from pathlib import Path

MAX_ROWS = 12        # firmware GH_MAX_ITEMS
PAYLOAD_LIMIT = 480  # firmware RX buffer is 512 bytes
REF_MAX, TITLE_MAX = 36, 64

# launchd runs the daemon with a bare PATH (/usr/bin:/bin:...), so look in
# the usual Homebrew locations too.
_GH_CANDIDATES = ("/opt/homebrew/bin/gh", "/usr/local/bin/gh")


def _gh() -> str | None:
    found = shutil.which("gh")
    if found:
        return found
    return next((p for p in _GH_CANDIDATES if Path(p).exists()), None)


def is_available() -> bool:
    return _gh() is not None


def _ascii(s: str, limit: int) -> str:
    folded = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    folded = " ".join(folded.split())
    return folded if len(folded) <= limit else folded[: limit - 3].rstrip() + "..."


def _run_gh(*args: str) -> str:
    gh = _gh()
    if not gh:
        raise RuntimeError("gh not found")
    res = subprocess.run([gh, *args], capture_output=True, text=True, timeout=30)
    if res.returncode != 0:
        raise RuntimeError(res.stderr.strip()[:200] or f"gh exited {res.returncode}")
    return res.stdout


def _review_requests() -> list[dict]:
    """Open, ready-for-review PRs requesting the user's review, newest first."""
    out = _run_gh("search", "prs", "--review-requested=@me", "--state=open",
                  "--draft=false", "--sort=updated",
                  "--json", "number,title,repository,url,author", "--limit", "50")
    return [{
        "ref": f"{pr['repository']['name']}#{pr['number']}",
        "title": pr.get("title", ""),
        "author": (pr.get("author") or {}).get("login", ""),
        "url": pr["url"],
    } for pr in json.loads(out or "[]")]


def _chunks(count: int, rows: list[dict], blip: bool) -> list[dict]:
    """Split rows into {"gh":{...}} payloads that each fit one BLE write."""
    # Third field is the row's top-left label (the author here).
    items = [[_ascii(r["ref"], REF_MAX), _ascii(r["title"], TITLE_MAX), _ascii(r["author"], 15)]
             for r in rows]
    out: list[dict] = []
    off = 0
    while True:
        msg = {"n": count, "o": off, "t": len(items), "i": []}
        if blip and not out:
            msg["b"] = 1
        while off < len(items):
            msg["i"].append(items[off])
            if len(json.dumps({"gh": msg}, separators=(",", ":"))) > PAYLOAD_LIMIT:
                msg["i"].pop()
                break
            off += 1
        out.append({"gh": msg})
        if off >= len(items) or not msg["i"]:
            return out


class Watcher:
    """Builds the device payloads and flags review requests that are new.

    Lives for the whole daemon run (not per BLE connection), and the first
    poll only records a baseline, so a restart or reconnect never replays a
    blip for requests that were already waiting. Seen PRs accumulate, so one
    that drops off and comes back (re-requested after a review) stays quiet.
    """

    def __init__(self) -> None:
        self.seen_reviews: set[str] | None = None
        self.urls: list[str] = []   # browser URL per row of the last payload

    def open_row(self, row: int) -> str | None:
        """Open the PR shown on device row ``row``; returns its URL."""
        if not 0 <= row < len(self.urls):
            return None
        subprocess.run(["open", self.urls[row]], check=True, timeout=10)
        return self.urls[row]

    def poll(self) -> list[dict] | None:
        if not is_available():
            return None
        reviews = _review_requests()
        review_urls = {r["url"] for r in reviews}
        rows = reviews[:MAX_ROWS]

        new = review_urls - self.seen_reviews if self.seen_reviews is not None else set()
        self.seen_reviews = (self.seen_reviews or set()) | review_urls
        self.urls = [r["url"] for r in rows]
        return _chunks(len(reviews), rows, bool(new) and blip_enabled())


CONFIG_FILE = Path.home() / ".config" / "claude-usage-monitor" / "config"


def blip_enabled() -> bool:
    """`review_blip = off` in the daemon config silences the review blip."""
    try:
        for line in CONFIG_FILE.read_text().splitlines():
            key, sep, val = line.partition("=")
            if sep and key.strip().lower() == "review_blip":
                return val.split("#")[0].strip().lower() != "off"
    except OSError:
        pass
    return True


if __name__ == "__main__":
    for msg in Watcher().poll() or []:
        print(json.dumps(msg, separators=(",", ":")))
