#!/usr/bin/env python3
"""
Tuya OpenAPI (Highway) CLI tool for user sync and device pairing.

Environment variables:
    TUYA_CLIENT_ID      - Highway client ID
    TUYA_CLIENT_SECRET  - Highway client secret
    TUYA_BASE_URL       - API base URL (e.g. https://openapi.tuyacn.com)
    TUYA_SCHEMA         - App schema (default for sync-user)
"""

import argparse
import hashlib
import hmac
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


class TuyaOpenAPIClient:

    def __init__(self, client_id, secret, base_url):
        self.client_id = client_id
        self.secret = secret
        self.base_url = base_url.rstrip("/")
        self.access_token = None

    def _sha256(self, data):
        return hashlib.sha256((data or "").encode("utf-8")).hexdigest()

    def _hmac_sha256(self, key, msg):
        return hmac.new(
            key.encode("utf-8"), msg.encode("utf-8"), hashlib.sha256
        ).hexdigest().upper()

    def _calc_sign(self, access_token, timestamp, method, path, body):
        content_sha256 = self._sha256(body)
        headers_str = ""
        str_to_sign = f"{method.upper()}\n{content_sha256}\n{headers_str}\n{path}"
        sign_src = f"{self.client_id}{access_token or ''}{timestamp}{str_to_sign}"
        return self._hmac_sha256(self.secret, sign_src)

    def _http_request(self, method, url, headers, body=None):
        data = body.encode("utf-8") if body else None
        req = urllib.request.Request(url, data=data, headers=headers, method=method.upper())
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            return json.loads(e.read().decode("utf-8"))

    def get_token(self):
        path = "/v1.0/token?grant_type=1"
        timestamp = str(int(time.time() * 1000))
        sign = self._calc_sign("", timestamp, "GET", path, None)
        headers = {
            "client_id": self.client_id,
            "t": timestamp,
            "sign_method": "HMAC-SHA256",
            "sign": sign,
        }
        resp = self._http_request("GET", self.base_url + path, headers)
        if resp.get("success"):
            self.access_token = resp["result"]["access_token"]
            return self.access_token
        else:
            print(f"Error getting token: {json.dumps(resp, indent=2)}", file=sys.stderr)
            sys.exit(1)

    def request(self, method, path, body=None, params=None):
        if not self.access_token:
            self.get_token()

        body_str = json.dumps(body) if body else None

        if params and method.upper() == "GET":
            qs = urllib.parse.urlencode(sorted(params.items()))
            sign_path = f"{path}?{qs}"
            url = f"{self.base_url}{path}?{qs}"
        else:
            sign_path = path
            url = self.base_url + path

        timestamp = str(int(time.time() * 1000))
        sign = self._calc_sign(self.access_token, timestamp, method, sign_path, body_str)
        headers = {
            "client_id": self.client_id,
            "access_token": self.access_token,
            "t": timestamp,
            "sign_method": "HMAC-SHA256",
            "sign": sign,
            "Content-Type": "application/json",
        }
        resp = self._http_request(method, url, headers, body_str)

        if resp.get("code") == 1010:
            self.get_token()
            timestamp = str(int(time.time() * 1000))
            sign = self._calc_sign(self.access_token, timestamp, method, sign_path, body_str)
            headers.update({
                "access_token": self.access_token,
                "t": timestamp,
                "sign": sign,
            })
            resp = self._http_request(method, url, headers, body_str)

        return resp


def cmd_sync_user(client, args):
    schema = args.schema or os.environ.get("TUYA_SCHEMA")
    if not schema:
        print("Error: --schema or TUYA_SCHEMA env var is required", file=sys.stderr)
        sys.exit(1)

    password_md5 = hashlib.md5(args.password.encode("utf-8")).hexdigest()

    body = {
        "country_code": args.country_code,
        "username": args.username,
        "password": password_md5,
        "username_type": args.username_type,
    }
    if args.nick_name:
        body["nick_name"] = args.nick_name
    if args.time_zone_id:
        body["time_zone_id"] = args.time_zone_id

    path = f"/v1.0/apps/{schema}/user"
    resp = client.request("POST", path, body=body)
    print(json.dumps(resp, indent=2, ensure_ascii=False))


def cmd_pairing_token(client, args):
    body = {
        "uid": args.uid,
        "paring_type": args.paring_type,
        "time_zone_id": args.time_zone_id,
    }
    if args.home_id:
        body["home_id"] = args.home_id
    if args.uuid:
        body["extension"] = {"uuid": args.uuid}

    resp = client.request("POST", "/v1.0/device/paring/token", body=body)
    print(json.dumps(resp, indent=2, ensure_ascii=False))


def cmd_pairing_result(client, args):
    path = f"/v1.0/device/paring/tokens/{args.token}"

    if args.poll:
        timeout = args.timeout
        interval = 1
        elapsed = 0
        while elapsed < timeout:
            resp = client.request("GET", path)
            result = resp.get("result", {})
            success_list = result.get("success", [])
            if success_list:
                print(json.dumps(resp, indent=2, ensure_ascii=False))
                return
            elapsed += interval
            if elapsed < timeout:
                print(f"Polling... {elapsed}s / {timeout}s", file=sys.stderr)
                time.sleep(interval)
        print(f"Timeout after {timeout}s, last response:", file=sys.stderr)
        print(json.dumps(resp, indent=2, ensure_ascii=False))
    else:
        resp = client.request("GET", path)
        print(json.dumps(resp, indent=2, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(
        description="Tuya OpenAPI CLI for user sync and device pairing"
    )
    parser.add_argument("--client-id", default=os.environ.get("TUYA_CLIENT_ID"),
                        help="Highway client ID (env: TUYA_CLIENT_ID)")
    parser.add_argument("--secret", default=os.environ.get("TUYA_CLIENT_SECRET"),
                        help="Highway client secret (env: TUYA_CLIENT_SECRET)")
    parser.add_argument("--base-url", default=os.environ.get("TUYA_BASE_URL", "https://openapi.tuyacn.com"),
                        help="API base URL (env: TUYA_BASE_URL)")

    sub = parser.add_subparsers(dest="command", required=True)

    # sync-user
    p_sync = sub.add_parser("sync-user", help="Create or update a user")
    p_sync.add_argument("--schema", default=None,
                        help="App schema identifier (env: TUYA_SCHEMA)")
    p_sync.add_argument("--country-code", required=True, help="Country code, e.g. 86")
    p_sync.add_argument("--username", required=True, help="Username (phone/email/other)")
    p_sync.add_argument("--password", required=True, help="Raw password (will be MD5 hashed)")
    p_sync.add_argument("--username-type", type=int, default=3,
                        choices=[1, 2, 3], help="1=phone, 2=email, 3=other (default: 3)")
    p_sync.add_argument("--nick-name", default=None, help="Nickname")
    p_sync.add_argument("--time-zone-id", default=None, help="Timezone, e.g. Asia/Shanghai")

    # pairing-token
    p_pair = sub.add_parser("pairing-token", help="Generate a device pairing token")
    p_pair.add_argument("--uid", required=True, help="Tuya user ID")
    p_pair.add_argument("--paring-type", required=True, choices=["BLE", "AP", "EZ"],
                        help="Pairing type")
    p_pair.add_argument("--time-zone-id", required=True, help="Timezone, e.g. Asia/Shanghai")
    p_pair.add_argument("--home-id", default=None, help="Home ID (optional)")
    p_pair.add_argument("--uuid", default=None, help="Device UUID (required for BLE)")

    # pairing-result
    p_result = sub.add_parser("pairing-result", help="Query pairing result by token")
    p_result.add_argument("--token", required=True, help="Pairing token")
    p_result.add_argument("--poll", action="store_true",
                          help="Poll until devices found or timeout")
    p_result.add_argument("--timeout", type=int, default=100,
                          help="Poll timeout in seconds (default: 100)")

    args = parser.parse_args()

    if not args.client_id or not args.secret:
        print("Error: --client-id/--secret or TUYA_CLIENT_ID/TUYA_CLIENT_SECRET env vars required",
              file=sys.stderr)
        sys.exit(1)

    client = TuyaOpenAPIClient(args.client_id, args.secret, args.base_url)

    if args.command == "sync-user":
        cmd_sync_user(client, args)
    elif args.command == "pairing-token":
        cmd_pairing_token(client, args)
    elif args.command == "pairing-result":
        cmd_pairing_result(client, args)


if __name__ == "__main__":
    main()
