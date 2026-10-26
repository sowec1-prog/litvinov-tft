import json
import threading
import unittest
from datetime import datetime, timezone
from http.server import ThreadingHTTPServer
from urllib.request import urlopen

from app import (
    ApiHandler,
    build_live_football_payload,
    build_live_hockey_payload,
    build_payload,
)


class TftServerTests(unittest.TestCase):
    def test_scheduled_payload_has_thin_client_contract(self):
        payload = build_payload("hockey", datetime(2026, 10, 25, 12, tzinfo=timezone.utc))
        self.assertEqual(payload["state"], "scheduled")
        self.assertEqual(payload["sport"], "hockey")
        self.assertEqual(payload["home"]["code"], "LIT")
        self.assertEqual(payload["away"]["logo"], "kometa")
        self.assertIn("match_start_epoch", payload)
        self.assertIn("server_epoch", payload)
        self.assertEqual(payload["timezone"], "Europe/Prague")

    def test_live_then_finished_state(self):
        self.assertEqual(build_payload("hockey", datetime(2026, 10, 27, 17, 30, tzinfo=timezone.utc))["state"], "live")
        self.assertEqual(build_payload("hockey", datetime(2026, 10, 27, 21, 30, tzinfo=timezone.utc))["state"], "finished")

    def test_normalizes_established_live_hockey_contract(self):
        payload = build_live_hockey_payload({
            "state": "live", "home_code": "LIT", "away_code": "KOM",
            "home_display": "HC VERVA Litvinov", "away_display": "Kometa",
            "score_home": 3, "score_away": 2, "game_clock": "2. tretina 11:45",
            "match_start_epoch": 1700000000, "server_epoch": 1700000100, "table_position": 5,
            "event_id": "goal-42", "audio_cue": "lit_goal",
        })
        self.assertEqual(payload["state"], "live")
        self.assertEqual(payload["home"]["code"], "LIT")
        self.assertEqual(payload["score_home"], 3)
        self.assertEqual(payload["status_text"], "2. tretina 11:45")
        self.assertEqual(payload["event_id"], "goal-42")
        self.assertEqual(payload["goal_side"], "home")

    def test_normalizes_sparta_official_calendar_match(self):
        official_page = r'''{\"match\":{\"id\":5742,\"state\":\"upcoming\",\"start\":\"2026-10-30T17:00:00Z\",\"home\":true,\"round\":\"10th round\"},\"team\":{\"id\":1,\"name\":\"Men A\"},\"opponent\":{\"id\":34,\"name\":\"FK Pardubice\",\"logo\":\"https://example.test/pardubice.png\",\"shortName\":\"PCE\"},\"stadium\":{\"id\":67,\"name\":\"epet ARENA\"},\"league\":{\"id\":1,\"name\":\"Chance LIGA\"}}'''
        payload = build_live_football_payload(
            official_page, now_epoch=1793376000  # 2026-10-30T16:00:00Z
        )
        self.assertEqual(payload["sport"], "football")
        self.assertEqual(payload["competition"], "Chance LIGA")
        self.assertEqual(payload["state"], "scheduled")
        self.assertEqual(payload["home"], {"code": "SPA", "name": "AC Sparta Praha", "logo": "sparta"})
        self.assertEqual(payload["away"]["code"], "PCE")
        self.assertEqual(payload["away"]["name"], "FK Pardubice")
        self.assertEqual(payload["match_start_epoch"], 1793379600)

    def test_http_endpoints(self):
        httpd = ThreadingHTTPServer(("127.0.0.1", 0), ApiHandler)
        worker = threading.Thread(target=httpd.serve_forever, daemon=True)
        worker.start()
        try:
            base = f"http://127.0.0.1:{httpd.server_port}"
            with urlopen(base + "/healthz", timeout=2) as response:
                self.assertTrue(json.load(response)["ok"])
            with urlopen(base + "/api/v1/sport/football", timeout=2) as response:
                payload = json.load(response)
            self.assertEqual(payload["sport"], "football")
            self.assertIn(payload["state"], {"scheduled", "live", "finished"})
        finally:
            httpd.shutdown()
            worker.join(timeout=2)
            httpd.server_close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
