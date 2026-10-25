import json
import threading
import unittest
from datetime import datetime, timezone
from http.server import ThreadingHTTPServer
from urllib.request import urlopen

from app import ApiHandler, build_payload


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
