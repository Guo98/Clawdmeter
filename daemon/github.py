"""GitHub PRs for the Clawdmeter PRs screen.

The badge counts open PRs waiting on the signed-in user's review. The list
shows those first, then other unread notifications on PRs the user
participates in (mentioned, authored, commented, assigned), up to MAX_ROWS.
Everything goes through the `gh` CLI, so it reuses whatever account
`gh auth login` set up and never handles a token itself.

The list is bigger than one BLE write (the firmware RX buffer is 512 bytes),
so it goes out as a few {"gh":{...,"o":offset,"t":rows}} chunks that the
device reassembles.
"""

import json
import re
import shutil
import subprocess
import unicodedata
from pathlib import Path

MAX_ROWS = 12        # firmware GH_MAX_ITEMS
PAYLOAD_LIMIT = 480  # firmware RX buffer is 512 bytes
REF_MAX, TITLE_MAX = 36, 64

REASONS = {
    "review_requested": "Review",
    "mention": "Mention",
    "team_mention": "Mention",
    "author": "Yours",
    "comment": "Comment",
    "assign": "Assigned",
    "state_change": "Updated",
    "ci_activity": "CI",
    "approval_requested": "Approval",
}

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
    """Open PRs requesting the user's (or their team's) review, newest first."""
    out = _run_gh("search", "prs", "--review-requested=@me", "--state=open",
                  "--sort=updated", "--json", "number,title,repository,url",
                  "--limit", "50")
    return [{
        "ref": f"{pr['repository']['name']}#{pr['number']}",
        "title": pr.get("title", ""),
        "reason": "Review",
        "url": pr["url"],
    } for pr in json.loads(out or "[]")]


_API_PR = re.compile(r"^https://api\.github\.com/repos/([^/]+/[^/]+)/pulls/(\d+)$")


def html_url(n: dict) -> str:
    """Browser URL for a PR notification (the API gives an api.github.com URL)."""
    m = _API_PR.match(n.get("subject", {}).get("url") or "")
    if m:
        return f"https://github.com/{m.group(1)}/pull/{m.group(2)}"
    return n.get("repository", {}).get("html_url") or "https://github.com/notifications"


def _notifications() -> list[dict]:
    """Unread PR notifications that involve the user, newest first. Review
    requests are left to the search above, which only sees open PRs."""
    notifs = json.loads(_run_gh("api", "/notifications?participating=true&per_page=50") or "[]")
    rows = []
    for n in notifs:
        subject = n.get("subject", {})
        reason = n.get("reason", "")
        if subject.get("type") != "PullRequest" or reason == "review_requested":
            continue
        number = (subject.get("url") or "").rsplit("/", 1)[-1]
        repo = n.get("repository", {}).get("name", "")
        rows.append({
            "ref": f"{repo}#{number}" if number.isdigit() else repo,
            "title": subject.get("title", ""),
            "reason": REASONS.get(reason, reason.replace("_", " ").title()[:12]),
            "url": html_url(n),
        })
    return rows


def _chunks(count: int, rows: list[dict], blip: bool) -> list[dict]:
    """Split rows into {"gh":{...}} payloads that each fit one BLE write."""
    items = [[_ascii(r["ref"], REF_MAX), _ascii(r["title"], TITLE_MAX), r["reason"]]
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
        others = [r for r in _notifications() if r["url"] not in review_urls]
        rows = (reviews + others)[:MAX_ROWS]

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
