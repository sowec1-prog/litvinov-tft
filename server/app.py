"""Local API bridge for the ESP32-S3 Litvínov/Sparta TFT panel."""
from __future__ import annotations

import json
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parent
FIXTURES = json.loads((ROOT / "fixtures.json").read_text(encoding="utf-8"))
PRAGUE = ZoneInfo("Europe/Prague")


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
