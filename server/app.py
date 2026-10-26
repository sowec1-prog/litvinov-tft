"""Local API bridge for the ESP32-S3 Litvínov/Sparta TFT panel."""
from __future__ import annotations

import json
import os
import re
import threading
import time
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse
from urllib.request import Request, urlopen
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parent
FIXTURES = json.loads((ROOT / "fixtures.json").read_text(encoding="utf-8"))
PRAGUE = ZoneInfo("Europe/Prague")
UPSTREAM_HOCKEY_URL = os.environ.get("TFT_HOCKEY_UPSTREAM", "https://litvinov-server.onrender.com/api/live")
# The ESP must never wait for Render cold starts. The bridge returns its last
# valid OLED payload immediately and refreshes it in a background thread.
_last_hockey_payload: dict | None = None
_last_hockey_refresh = 0.0
_hockey_refresh_inflight = False
_hockey_cache_lock = threading.Lock()
HOCKEY_REFRESH_SECONDS = 30


def fetch_json(url: str, timeout: int = 20) -> dict:
    request = Request(url, headers={"User-Agent": "litvinov-tft/1.0"})
    with urlopen(request, timeout=timeout) as response:
        return json.load(response)


def build_live_hockey_payload(upstream: dict, now_epoch: int | None = None) -> dict:
    """Normalize the established OLED live feed for the independent TFT API."""
    now_epoch = now_epoch or int(time.time())
    state = str(upstream.get("state", "scheduled"))
    home_code = str(upstream.get("home_code", "LIT"))
    away_code = str(upstream.get("away_code", "?"))
    goal_side = str(upstream.get("goal_side") or upstream.get("goal_team") or "")
    cue = str(upstream.get("audio_cue") or "")
    # OLED cues are Litvínov-centric; normalize them to the displayed home/away side.
    if goal_side not in {"home", "away"}:
        lit_is_home = home_code.upper() == "LIT"
        if cue == "lit_goal":
            goal_side = "home" if lit_is_home else "away"
        elif cue == "conceded_goal":
            goal_side = "away" if lit_is_home else "home"
    return {
        "api_version": 1,
        "sport": "hockey",
        "state": state if state in {"scheduled", "live", "finished"} else "scheduled",
        "competition": "Tipsport extraliga",
        "home": {"code": home_code, "name": upstream.get("home_display") or upstream.get("home", home_code), "logo": home_code.lower()},
        "away": {"code": away_code, "name": upstream.get("away_display") or upstream.get("away", away_code), "logo": away_code.lower()},
        "score_home": int(upstream.get("score_home", 0) or 0),
        "score_away": int(upstream.get("score_away", 0) or 0),
        "status_text": str(upstream.get("game_clock") or upstream.get("power_play") or ""),
        "match_start_epoch": int(upstream.get("match_start_epoch", 0) or 0),
        "server_epoch": int(upstream.get("server_epoch", now_epoch) or now_epoch),
        "table_position": int(upstream.get("table_position", 0) or 0),
        "event_id": str(upstream.get("event_id") or ""),
        "audio_cue": cue,
        "goal_side": goal_side,
        "timezone": "Europe/Prague",
        "source": "independent TFT bridge",
    }


def _refresh_hockey_from_oled() -> None:
    """Fetch OLED's established live feed without holding an ESP request open."""
    global _last_hockey_payload, _last_hockey_refresh, _hockey_refresh_inflight
    try:
        payload = build_live_hockey_payload(fetch_json(UPSTREAM_HOCKEY_URL, timeout=20))
        payload["cached"] = False
        with _hockey_cache_lock:
            _last_hockey_payload = payload
    except Exception:
        # Keep the last known good OLED contract; the fixture seed remains valid
        # during a Render cold start or a temporary upstream outage.
        pass
    finally:
        with _hockey_cache_lock:
            _last_hockey_refresh = time.monotonic()
            _hockey_refresh_inflight = False


def live_hockey_payload() -> dict:
    """Return instantly; refresh the same OLED source asynchronously."""
    global _hockey_refresh_inflight
    with _hockey_cache_lock:
        now = time.monotonic()
        if not _hockey_refresh_inflight and now - _last_hockey_refresh >= HOCKEY_REFRESH_SECONDS:
            _hockey_refresh_inflight = True
            threading.Thread(target=_refresh_hockey_from_oled, daemon=True).start()
        payload = dict(_last_hockey_payload or build_payload("hockey"))
    payload["cached"] = True
    return payload


UPSTREAM_FOOTBALL_URL = os.environ.get(
    "TFT_FOOTBALL_UPSTREAM", "https://sparta.cz/en/zapasy/1-muzi-a/2026-2027/kalendar"
)
_last_football_payload: dict | None = None
_last_football_refresh = 0.0
_football_refresh_inflight = False
_football_cache_lock = threading.Lock()
FOOTBALL_REFRESH_SECONDS = 300


def fetch_text(url: str, timeout: int = 20) -> str:
    request = Request(url, headers={"User-Agent": "litvinov-tft/1.0"})
    with urlopen(request, timeout=timeout) as response:
        return response.read().decode("utf-8")


def build_live_football_payload(official_page: str, now_epoch: int | None = None) -> dict:
    """Normalize AC Sparta's public calendar page for the compact TFT contract."""
    now_epoch = now_epoch or int(time.time())
    decoded = official_page.replace(r'\"', '"')
    matches = []
    decoder = json.JSONDecoder()
    for found in re.finditer(r'\{"match":', decoded):
        try:
            item, _ = decoder.raw_decode(decoded[found.start():])
        except json.JSONDecodeError:
            continue
        if {"match", "opponent", "league"} <= item.keys():
            matches.append(item)
    if not matches:
        raise ValueError("Sparta official calendar did not contain matches")
    live = [item for item in matches if item["match"].get("state") == "live"]
    future = [item for item in matches if datetime.fromisoformat(item["match"]["start"].replace("Z", "+00:00")).timestamp() >= now_epoch]
    selected = (live or sorted(future, key=lambda item: item["match"]["start"]) or matches)[0]
    match, opponent, league = selected["match"], selected["opponent"], selected["league"]
    start_epoch = int(datetime.fromisoformat(match["start"].replace("Z", "+00:00")).timestamp())
    home = bool(match.get("home"))
    return {
        "api_version": 1,
        "sport": "football",
        "state": "live" if match.get("state") == "live" else ("scheduled" if now_epoch < start_epoch else "finished"),
        "competition": str(league.get("name") or "Fotbal"),
        "home": {"code": "SPA", "name": "AC Sparta Praha", "logo": "sparta"} if home else {"code": str(opponent.get("shortName") or "OPP"), "name": str(opponent.get("name") or "Soupeř"), "logo": str(opponent.get("logo") or "")},
        "away": {"code": str(opponent.get("shortName") or "OPP"), "name": str(opponent.get("name") or "Soupeř"), "logo": str(opponent.get("logo") or "")} if home else {"code": "SPA", "name": "AC Sparta Praha", "logo": "sparta"},
        "score_home": 0,
        "score_away": 0,
        "status_text": str(match.get("round") or ""),
        "match_start_epoch": start_epoch,
        "server_epoch": now_epoch,
        "timezone": "Europe/Prague",
        "source": "AC Sparta Praha official calendar",
    }


def _refresh_football_from_sparta() -> None:
    global _last_football_payload, _last_football_refresh, _football_refresh_inflight
    try:
        payload = build_live_football_payload(fetch_text(UPSTREAM_FOOTBALL_URL, timeout=20))
        payload["cached"] = False
        with _football_cache_lock:
            _last_football_payload = payload
    except Exception:
        pass
    finally:
        with _football_cache_lock:
            _last_football_refresh = time.monotonic()
            _football_refresh_inflight = False


def live_football_payload() -> dict:
    global _football_refresh_inflight
    with _football_cache_lock:
        if not _football_refresh_inflight and time.monotonic() - _last_football_refresh >= FOOTBALL_REFRESH_SECONDS:
            _football_refresh_inflight = True
            threading.Thread(target=_refresh_football_from_sparta, daemon=True).start()
        payload = dict(_last_football_payload or build_payload("football"))
    payload["cached"] = True
    return payload


def utc_epoch(value: str) -> int:
    return int(datetime.fromisoformat(value).astimezone(timezone.utc).timestamp())


def build_payload(sport: str, now: datetime | None = None) -> dict:
    """Return the compact, stable contract consumed by the TFT."""
    if sport not in FIXTURES:
        raise KeyError(sport)
    fixture = FIXTURES[sport]
    now = now or datetime.now(timezone.utc)
    if now.tzinfo is None:
        raise ValueError("now must include a timezone")
    start = datetime.fromisoformat(fixture["match_start"])
    start_epoch = int(start.timestamp())
    now_epoch = int(now.timestamp())
    local_now = now.astimezone(PRAGUE)
    local_start = start.astimezone(PRAGUE)
    if now_epoch < start_epoch:
        state = "scheduled"
    elif now_epoch < start_epoch + fixture["match_duration_seconds"]:
        state = "live"
    else:
        state = "finished"
    return {
        "api_version": 1,
        "state": state,
        "sport": sport,
        "competition": fixture["competition"],
        "home": fixture["home"],
        "away": fixture["away"],
        "match_start_epoch": start_epoch,
        "server_epoch": now_epoch,
        "is_match_day": local_now.date() == local_start.date(),
        "timezone": "Europe/Prague",
    }


class ApiHandler(BaseHTTPRequestHandler):
    server_version = "LitvinovTftServer/0.1"

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"{self.address_string()} - {fmt % args}")

    def reply_json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/healthz":
            self.reply_json(HTTPStatus.OK, {"ok": True, "service": "litvinov-tft-server"})
            return
        if path == "/api/v1/sports":
            self.reply_json(HTTPStatus.OK, {"sports": sorted(FIXTURES), "api_version": 1})
            return
        prefix = "/api/v1/sport/"
        if path.startswith(prefix):
            sport = path.removeprefix(prefix)
            try:
                if sport == "hockey":
                    self.reply_json(HTTPStatus.OK, live_hockey_payload())
                elif sport == "football":
                    self.reply_json(HTTPStatus.OK, live_football_payload())
                else:
                    self.reply_json(HTTPStatus.OK, build_payload(sport))
            except KeyError:
                self.reply_json(HTTPStatus.NOT_FOUND, {"error": "unknown_sport"})
            return
        self.reply_json(HTTPStatus.NOT_FOUND, {"error": "not_found"})


def run(host: str = "127.0.0.1", port: int = 8080) -> None:
    httpd = ThreadingHTTPServer((host, port), ApiHandler)
    print(f"TFT server listening on http://{host}:{port}")
    httpd.serve_forever()


if __name__ == "__main__":
    run()
