"""GitHub PR notifications for the Clawdmeter GitHub screen.

Lists the signed-in user's unread notifications on pull requests they
participate in (review requested, mentioned, authored, commented, assigned)
via the `gh` CLI, so it reuses whatever account `gh auth login` set up and
never handles a token itself.
"""

import json
import shutil
import subprocess
import unicodedata
from pathlib import Path

MAX_ITEMS = 4        # firmware GH_MAX_ITEMS
MAX_PAGES = 3        # count up to 150; the display caps at "99+" anyway
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


def _fetch() -> list[dict] | None:
    gh = _gh()
    if not gh:
        return None
    # Page by hand rather than --paginate: a backlog of hundreds of unread
    # notifications would otherwise cost a dozen requests every poll.
    notifs: list[dict] = []
    for page in range(1, MAX_PAGES + 1):
        res = subprocess.run(
            [gh, "api", f"/notifications?participating=true&per_page=50&page={page}"],
            capture_output=True, text=True, timeout=30,
        )
        if res.returncode != 0:
            raise RuntimeError(res.stderr.strip()[:200] or f"gh exited {res.returncode}")
        batch = json.loads(res.stdout or "[]")
        notifs.extend(batch)
        if len(batch) < 50:
            break
    return notifs


def payload() -> dict | None:
    """The {"gh": {...}} device payload, or None if gh is missing.

    Raises RuntimeError when gh fails (e.g. signed out) so the caller can log it.
    """
    notifs = _fetch()
    if notifs is None:
        return None
    prs = [n for n in notifs if n.get("subject", {}).get("type") == "PullRequest"]
    items = []
    for n in prs[:MAX_ITEMS]:
        repo = n.get("repository", {}).get("name", "")
        number = (n.get("subject", {}).get("url") or "").rsplit("/", 1)[-1]
        ref = f"{repo}#{number}" if number.isdigit() else repo
        reason = n.get("reason", "")
        items.append([
            _ascii(ref, REF_MAX),
            _ascii(n.get("subject", {}).get("title", ""), TITLE_MAX),
            REASONS.get(reason, reason.replace("_", " ").title()[:12]),
        ])
    out = {"gh": {"n": len(prs), "i": items}}
    # Keep under the firmware's RX buffer; drop the oldest rows if needed.
    while items and len(json.dumps(out, separators=(",", ":"))) > PAYLOAD_LIMIT:
        items.pop()
    return out


if __name__ == "__main__":
    print(json.dumps(payload(), indent=2))
