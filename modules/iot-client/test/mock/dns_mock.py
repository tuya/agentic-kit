#!/usr/bin/env python3
"""
IoT DNS Mock Server

Simulates the IoT DNS service for testing:
  - POST /v1/dns_query
  - POST /v2/url_config
  - GET  /api/v1/ca-certificate

Listens on http://127.0.0.1:8198 (plain HTTP) by default.
"""

import json
import os
import ssl
import sys
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs


class ReusableHTTPServer(HTTPServer):
    allow_reuse_address = True

MOCK_HOST = "127.0.0.1"
MOCK_PORT = 8198

FAKE_CA_CERT = (
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBa"
)

DNS_DB = {
    "a1.tuyacn.com": {
        "Ips": ["47.100.1.100", "47.100.1.101"],
        "ip6s": ["2001:db8::1"],
        "ttl": 600,
    },
    "m1.tuyacn.com": {
        "Ips": ["47.100.2.200"],
        "ip6s": [],
        "ttl": 300,
    },
}

URL_CONFIG_ENDPOINTS = {
    "httpsUrl": {
        "addr": "https://a1-test.example.com/d.json",
        "ips": ["10.0.0.1", "10.0.0.2"],
    },
    "httpsPSKUrl": {
        "addr": "https://a3-test.example.com/d.json",
        "ips": ["10.0.0.3"],
    },
    "mqttsPSK3Url": {
        "addr": "m3-test.example.com:8883",
        "ips": ["10.0.1.3"],
    },
    "httpUrl": {
        "addr": "http://a1-test.example.com/d.json",
        "ips": ["10.0.0.1"],
    },
    "mqttUrl": {
        "addr": "127.0.0.1:1883",
        "ips": ["127.0.0.1"],
    },
    "mqttsUrl": {
        "addr": f"127.0.0.1:{os.getenv('ONBOARDING_MQTT_MOCK_PORT', '11884')}",
        "ips": ["127.0.0.1"],
    },
}


class DNSMockHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"  [DNS_MOCK] {fmt % args}", file=sys.stderr)

    def _send_json(self, data, status=200):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    # ---- POST handlers ----

    def do_POST(self):
        content_len = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(content_len) if content_len else b""

        if self.path == "/v1/dns_query":
            self._handle_dns_query(raw)
        elif self.path == "/v2/url_config":
            self._handle_url_config(raw)
        else:
            self._send_json({"error": "not found"}, 404)

    def _handle_dns_query(self, raw):
        try:
            domains = json.loads(raw)
        except json.JSONDecodeError:
            self._send_json({"error": "bad json"}, 400)
            return

        result = {}
        for item in domains:
            domain = item.get("domain", "")
            need_ip6 = item.get("need_ip6", False)
            entry = DNS_DB.get(domain)
            if entry:
                r = {"Ips": entry["Ips"], "ttl": entry["ttl"]}
                if need_ip6:
                    r["ip6s"] = entry.get("ip6s", [])
                result[domain] = r
            else:
                result[domain] = {"Ips": [], "ttl": 0}

        self._send_json(result)

    def _handle_url_config(self, raw):
        try:
            req = json.loads(raw)
        except json.JSONDecodeError:
            self._send_json({"error": "bad json"}, 400)
            return

        missing = [f for f in ("env", "uuid") if not req.get(f)]
        if missing:
            self._send_json({"error": f"missing required fields: {', '.join(missing)}"}, 400)
            return

        config = req.get("config", [])
        result = {
            "ttl": 600,
            "psk_key": "",
        }

        need_ca = False
        for item in config:
            if item.get("need_ca"):
                need_ca = True
                break
        if need_ca:
            result["caArr"] = [FAKE_CA_CERT]

        for item in config:
            key = item.get("key", "")
            ep = URL_CONFIG_ENDPOINTS.get(key)
            if ep:
                entry = {"addr": ep["addr"], "ips": ep["ips"]}
                if item.get("need_ip6"):
                    entry["ip6s"] = ["fe80::1"]
                result[key] = entry

        self._send_json(result)

    # ---- GET handlers ----

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/v1/ca-certificate":
            self._handle_ca_cert(parsed)
        else:
            self._send_json({"error": "not found"}, 404)

    def _handle_ca_cert(self, parsed):
        qs = parse_qs(parsed.query)
        host = qs.get("host", [None])[0]
        if not host:
            self._send_json({"ca_certificate": ""})
            return
        self._send_json({"ca_certificate": FAKE_CA_CERT})


def main():
    port = int(os.getenv("DNS_MOCK_PORT", str(MOCK_PORT)))
    server = ReusableHTTPServer((MOCK_HOST, port), DNSMockHandler)

    use_ssl = os.getenv("DNS_MOCK_USE_SSL", "").lower() in ("1", "true", "yes")
    if use_ssl:
        config_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "config")
        cert_file = os.getenv("DNS_MOCK_CERT", os.path.join(config_dir, "cert.pem"))
        key_file = os.getenv("DNS_MOCK_KEY", os.path.join(config_dir, "key.pem"))
        if os.path.exists(cert_file) and os.path.exists(key_file):
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.set_ciphers("ECDHE+AESGCM")
            ctx.load_cert_chain(cert_file, key_file)
            server.socket = ctx.wrap_socket(server.socket, server_side=True)
            print(f"DNS Mock Server on https://{MOCK_HOST}:{port}", file=sys.stderr)
        else:
            print(f"DNS Mock Server on http://{MOCK_HOST}:{port} (cert missing)", file=sys.stderr)
    else:
        print(f"DNS Mock Server on http://{MOCK_HOST}:{port}", file=sys.stderr)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
