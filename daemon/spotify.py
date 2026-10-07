#!/usr/bin/env python3
"""Spotify now-playing for the Clawdmeter media screen.

Reads the signed-in user's current track from the Spotify Web API (works for
the web player, desktop app, or a phone) and converts album art to the raw
RGB565 the firmware draws.

One-time sign-in (opens a browser; uses PKCE, so no client secret):

    daemon/.venv/bin/python daemon/spotify.py login <client-id>

The Spotify app (developer.spotify.com) must list the redirect URI
http://127.0.0.1:8765/callback. Development-mode apps need the owner to have
Spotify Premium (Spotify policy since Feb 2026).
"""

import base64
import hashlib
import http.server
import json
import os
import secrets
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unicodedata
import urllib.parse
import webbrowser
from pathlib import Path

import httpx

TOKEN_FILE = Path.home() / ".config" / "claude-usage-monitor" / "spotify.json"
REDIRECT_PORT = 8765
REDIRECT_URI = f"http://127.0.0.1:{REDIRECT_PORT}/callback"
SCOPES = "user-read-currently-playing user-read-playback-state"
AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
NOW_PLAYING_URL = "https://api.spotify.com/v1/me/player/currently-playing"

# Firmware np_state_t
NP_OFF, NP_PAUSED, NP_PLAYING = 0, 1, 2

# The firmware's fonts are ASCII-only, and its title/artist buffers are 96/64
# bytes. Fold accents ("Beyoncé" -> "Beyonce") rather than dropping letters.
TITLE_MAX, ARTIST_MAX = 90, 60


def is_configured() -> bool:
    return TOKEN_FILE.exists()


def _ascii(s: str, limit: int) -> str:
    folded = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    folded = " ".join(folded.split())
    return folded if len(folded) <= limit else folded[: limit - 3].rstrip() + "..."


def _save_tokens(data: dict) -> None:
    TOKEN_FILE.parent.mkdir(parents=True, exist_ok=True)
    tmp = TOKEN_FILE.with_suffix(".tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(data, f)
    tmp.replace(TOKEN_FILE)


class SpotifyClient:
    """Polls the current track, refreshing the access token as needed."""

    def __init__(self) -> None:
        self._tokens = json.loads(TOKEN_FILE.read_text())
        self._access: str | None = None
        self._access_exp = 0.0
        self._backoff_until = 0.0

    async def _access_token(self, http: httpx.AsyncClient) -> str | None:
        if self._access and time.time() < self._access_exp - 60:
            return self._access
        r = await http.post(TOKEN_URL, data={
            "grant_type": "refresh_token",
            "refresh_token": self._tokens["refresh_token"],
            "client_id": self._tokens["client_id"],
        })
        if r.status_code != 200:
            raise RuntimeError(f"Spotify token refresh failed: {r.status_code} {r.text[:200]}")
        body = r.json()
        self._access = body["access_token"]
        self._access_exp = time.time() + body.get("expires_in", 3600)
        # PKCE refresh tokens rotate; persist the new one or the next restart
        # would be locked out.
        if body.get("refresh_token") and body["refresh_token"] != self._tokens["refresh_token"]:
            self._tokens["refresh_token"] = body["refresh_token"]
            _save_tokens(self._tokens)
        return self._access

    async def now_playing(self) -> dict | None:
        """Return {"s", "t", "a", "art"} or None on a transient failure.

        "art" is a list of (width, url) for the album or episode images.
        """
        if time.time() < self._backoff_until:
            return None
        async with httpx.AsyncClient(timeout=10) as http:
            token = await self._access_token(http)
            r = await http.get(
                NOW_PLAYING_URL,
                params={"additional_types": "episode"},
                headers={"Authorization": f"Bearer {token}"},
            )
        if r.status_code == 204:
            return {"s": NP_OFF, "t": "", "a": "", "art": []}
        if r.status_code == 401:
            self._access = None   # expired early; refresh on the next poll
            return None
        if r.status_code == 429:
            self._backoff_until = time.time() + int(r.headers.get("Retry-After", "30"))
            return None
        if r.status_code != 200:
            return None
        body = r.json()
        item = body.get("item")
        if not item:
            return {"s": NP_OFF, "t": "", "a": "", "art": []}
        if item.get("type") == "episode":
            artist = item.get("show", {}).get("name", "")
            images = item.get("images") or item.get("show", {}).get("images", [])
        else:
            artist = ", ".join(a["name"] for a in item.get("artists", []))
            images = item.get("album", {}).get("images", [])
        return {
            "s": NP_PLAYING if body.get("is_playing") else NP_PAUSED,
            "t": _ascii(item.get("name", ""), TITLE_MAX),
            "a": _ascii(artist, ARTIST_MAX),
            "art": [(img.get("width") or 0, img["url"]) for img in images],
        }


def pick_art_url(images: list[tuple[int, str]], px: int) -> str | None:
    """Smallest image at least px wide (Spotify serves 64 / 300 / 640)."""
    if not images:
        return None
    big_enough = [im for im in images if im[0] >= px]
    return min(big_enough)[1] if big_enough else max(images)[1]


def _bmp_to_rgb565(bmp: bytes, px: int) -> bytes:
    """Decode an uncompressed 24/32-bit BMP (what sips writes) to RGB565 LE."""
    if bmp[:2] != b"BM":
        raise ValueError("not a BMP")
    data_off = struct.unpack_from("<I", bmp, 10)[0]
    w, h = struct.unpack_from("<ii", bmp, 18)
    bpp = struct.unpack_from("<H", bmp, 28)[0]
    if bpp not in (24, 32) or w != px or abs(h) != px:
        raise ValueError(f"unexpected BMP {w}x{h}@{bpp}")
    bottom_up = h > 0
    step = bpp // 8
    row_len = (w * step + 3) & ~3
    out = bytearray(px * px * 2)
    o = 0
    for y in range(px):
        src_row = (px - 1 - y) if bottom_up else y
        base = data_off + src_row * row_len
        for x in range(px):
            b, g, r = bmp[base + x * step: base + x * step + 3]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out[o] = v & 0xFF
            out[o + 1] = v >> 8
            o += 2
    return bytes(out)


async def fetch_art_rgb565(url: str, px: int) -> bytes | None:
    """Download a cover and scale it to px×px RGB565 using macOS `sips`."""
    async with httpx.AsyncClient(timeout=15) as http:
        r = await http.get(url)
    if r.status_code != 200:
        return None
    with tempfile.TemporaryDirectory() as d:
        src, dst = Path(d) / "cover", Path(d) / "cover.bmp"
        src.write_bytes(r.content)
        res = subprocess.run(
            ["sips", "-s", "format", "bmp", "-z", str(px), str(px), str(src), "--out", str(dst)],
            capture_output=True, timeout=20,
        )
        if res.returncode != 0 or not dst.exists():
            return None
        return _bmp_to_rgb565(dst.read_bytes(), px)


# ---------------------------------------------------------------------------
# One-time login (Authorization Code + PKCE)
# ---------------------------------------------------------------------------

def login(client_id: str) -> None:
    verifier = secrets.token_urlsafe(64)
    challenge = base64.urlsafe_b64encode(
        hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
    state = secrets.token_urlsafe(16)
    result: dict = {}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            if q.get("state", [None])[0] == state:
                result["code"] = q.get("code", [None])[0]
                result["error"] = q.get("error", [None])[0]
            ok = bool(result.get("code"))
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            msg = ("Clawdmeter is connected to Spotify. You can close this tab."
                   if ok else "Spotify sign-in failed. Check the terminal.")
            self.wfile.write(f"<p style='font:16px sans-serif'>{msg}</p>".encode())

        def log_message(self, *args):
            pass

    server = http.server.HTTPServer(("127.0.0.1", REDIRECT_PORT), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    url = AUTH_URL + "?" + urllib.parse.urlencode({
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "code_challenge_method": "S256",
        "code_challenge": challenge,
        "scope": SCOPES,
        "state": state,
    })
    print("Opening Spotify sign-in in your browser...")
    print(f"If it doesn't open, visit:\n  {url}\n")
    webbrowser.open(url)

    deadline = time.time() + 300
    while "code" not in result and "error" not in result and time.time() < deadline:
        time.sleep(0.2)
    server.shutdown()

    if not result.get("code"):
        sys.exit(f"Sign-in failed: {result.get('error') or 'timed out'}")

    r = httpx.post(TOKEN_URL, data={
        "grant_type": "authorization_code",
        "code": result["code"],
        "redirect_uri": REDIRECT_URI,
        "client_id": client_id,
        "code_verifier": verifier,
    }, timeout=15)
    if r.status_code != 200:
        sys.exit(f"Token exchange failed: {r.status_code} {r.text[:300]}")
    _save_tokens({"client_id": client_id, "refresh_token": r.json()["refresh_token"]})
    print(f"Saved Spotify sign-in to {TOKEN_FILE}")
    print("Restart the daemon to pick it up:")
    print("  launchctl kickstart -k gui/$(id -u)/com.user.claude-usage-daemon")


async def _show_now_playing() -> None:
    np = await SpotifyClient().now_playing()
    print(json.dumps(np, indent=2))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "login":
        login(sys.argv[2])
    elif len(sys.argv) == 2 and sys.argv[1] == "now":
        import asyncio
        asyncio.run(_show_now_playing())
    else:
        sys.exit("usage: spotify.py login <client-id> | spotify.py now")
