#!/usr/bin/env python3
"""Simple test server to receive gateway miner data."""

import json
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler


class IngestHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/api/ingest":
            self.send_response(404)
            self.end_headers()
            return

        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)

        try:
            data = json.loads(body)
            ts = datetime.now().strftime("%H:%M:%S")
            miners = data.get("miners", [])
            gateway = data.get("gateway", {})

            print(f"\n{'='*60}")
            print(f"[{ts}] Received payload from {self.client_address[0]}")
            print(f"  Gateway: {gateway.get('hostname', '?')} ({gateway.get('ip', '?')})")
            print(f"  Miners:  {len(miners)}")

            for m in miners:
                status = "ONLINE" if m.get("hashrate", 0) > 0 else "OFFLINE"
                print(f"    - {m.get('hostname', '?'):20s} {m.get('ip', '?'):15s} "
                      f"{m.get('hashrate', 0):8.2f} GH/s  {m.get('temp', 0):5.1f}C  "
                      f"{m.get('power', 0):5.1f}W  [{status}]")

            print(f"{'='*60}")
        except json.JSONDecodeError:
            print(f"Bad JSON: {body[:200]}")

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"ok":true}')

    def log_message(self, format, *args):
        pass  # suppress default request logging


if __name__ == "__main__":
    port = 8888
    server = HTTPServer(("0.0.0.0", port), IngestHandler)
    print(f"Test server listening on http://0.0.0.0:{port}/api/ingest")
    print("Waiting for gateway data...\n")
    server.serve_forever()
