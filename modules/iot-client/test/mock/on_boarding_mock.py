#!/usr/bin/env python3
"""
MQTT mock for on_boarding_with_qrcode tests.

After a client subscribes, pushes a canned activation JSON message
on the subscribed topic so the C test can verify the full onboarding flow.
"""

import socket
import struct
import threading
import ssl
import os
import sys
import time
import hashlib

MQTT_CONNECT    = 0x10
MQTT_CONNACK    = 0x20
MQTT_PUBLISH    = 0x30
MQTT_PUBACK     = 0x40
MQTT_SUBSCRIBE  = 0x80
MQTT_SUBACK     = 0x90
MQTT_PINGREQ    = 0xC0
MQTT_PINGRESP   = 0xD0
MQTT_DISCONNECT = 0xE0

HOST = "127.0.0.1"
PORT = int(os.getenv("ONBOARDING_MQTT_MOCK_PORT", "11884"))

EXPECTED_AUTHKEY = "ci_authkey_1234567890abcdef"

ACTIVATION_JSON = '{"data":{"httpsUrl":"https://127.0.0.1:%s","region":"AY","token":"mock_activation_token_12345"}}' % os.getenv("ATOP_MOCK_PORT", "8443")


def compute_mqtt_password(authkey):
    """Compute MQTT password: md5(authkey)[4:12] as hex (16 chars)."""
    md5 = hashlib.md5(authkey.encode("utf-8")).digest()
    return md5[4:12].hex()


def encode_remaining_length(length):
    result = bytearray()
    while True:
        byte = length % 128
        length //= 128
        if length > 0:
            byte |= 0x80
        result.append(byte)
        if length == 0:
            break
    return bytes(result)


def parse_remaining_length(sock):
    multiplier = 1
    value = 0
    while True:
        b = sock.recv(1)
        if not b:
            return -1
        byte = b[0]
        value += (byte & 0x7F) * multiplier
        if (byte & 0x80) == 0:
            break
        multiplier *= 128
    return value


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def parse_string(data, offset):
    length = struct.unpack(">H", data[offset:offset + 2])[0]
    offset += 2
    s = data[offset:offset + length]
    return s, offset + length


def encode_string(s):
    if isinstance(s, str):
        s = s.encode("utf-8")
    return struct.pack(">H", len(s)) + s


def parse_connect_payload(payload):
    """Parse MQTT 3.1.1 CONNECT variable header + payload."""
    off = 0
    _proto_name, off = parse_string(payload, off)
    connect_flags = payload[off + 1]
    off += 2
    off += 2  # Keep Alive
    has_username = bool(connect_flags & 0x80)
    has_password = bool(connect_flags & 0x40)
    has_will = bool(connect_flags & 0x04)
    client_id, off = parse_string(payload, off)
    if has_will:
        _will_topic, off = parse_string(payload, off)
        _will_msg, off = parse_string(payload, off)
    username = None
    if has_username:
        username, off = parse_string(payload, off)
    password = None
    if has_password:
        password, off = parse_string(payload, off)
    decode = lambda b: b.decode("utf-8") if isinstance(b, bytes) else b
    return decode(client_id), decode(username), decode(password)


def build_publish_packet(topic, payload):
    """Build a QoS 0 MQTT 3.1.1 PUBLISH packet."""
    topic_bytes = encode_string(topic)
    if isinstance(payload, str):
        payload = payload.encode("utf-8")
    var_and_payload = topic_bytes + payload
    remaining = encode_remaining_length(len(var_and_payload))
    return bytes([MQTT_PUBLISH]) + remaining + var_and_payload


def handle_client(client_sock, addr):
    try:
        while True:
            hdr = client_sock.recv(1)
            if not hdr:
                break

            header_byte = hdr[0]
            pkt_type = header_byte & 0xF0
            remaining = parse_remaining_length(client_sock)
            if remaining < 0:
                break

            payload = recv_exact(client_sock, remaining) if remaining > 0 else b""
            if payload is None and remaining > 0:
                break

            if pkt_type == MQTT_CONNECT:
                client_id, username, password = parse_connect_payload(payload)
                expected_pw = compute_mqtt_password(EXPECTED_AUTHKEY)
                if password != expected_pw:
                    print(f"  AUTH FAIL: client={client_id} password={password!r} expected={expected_pw}", flush=True)
                    connack = bytes([MQTT_CONNACK, 2, 0, 4])  # 4 = bad credentials
                    client_sock.sendall(connack)
                    break
                print(f"  AUTH OK: client={client_id} username={username}", flush=True)
                connack = bytes([MQTT_CONNACK, 2, 0, 0])
                client_sock.sendall(connack)

            elif pkt_type == MQTT_SUBSCRIBE:
                pkt_id = struct.unpack(">H", payload[0:2])[0]
                off = 2
                topic_bytes, off = parse_string(payload, off)
                subscribe_topic = topic_bytes.decode("utf-8") if isinstance(topic_bytes, bytes) else topic_bytes

                # MQTT 3.1.1 SUBACK: pkt_id(2) + return_code(1)
                suback = bytes([MQTT_SUBACK, 3]) + struct.pack(">H", pkt_id) + bytes([0])

                activation_pkt = build_publish_packet(subscribe_topic, ACTIVATION_JSON)

                # Send SUBACK + activation message together
                client_sock.sendall(suback + activation_pkt)

            elif pkt_type == MQTT_PUBLISH:
                qos = (header_byte & 0x06) >> 1
                if qos == 1:
                    topic_bytes, off = parse_string(payload, 0)
                    pkt_id = struct.unpack(">H", payload[off:off + 2])[0]
                    # MQTT 3.1.1 PUBACK: pkt_id(2)
                    puback = bytes([MQTT_PUBACK, 2]) + struct.pack(">H", pkt_id)
                    client_sock.sendall(puback)

            elif pkt_type == MQTT_PINGREQ:
                client_sock.sendall(bytes([MQTT_PINGRESP, 0]))

            elif pkt_type == MQTT_DISCONNECT:
                break

    except (ConnectionResetError, BrokenPipeError, OSError):
        pass
    finally:
        client_sock.close()


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(5)
    srv.settimeout(1.0)

    config_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'config')
    cert_file = os.path.join(config_dir, 'cert.pem')
    key_file = os.path.join(config_dir, 'key.pem')

    tls_ctx = None
    if os.path.exists(cert_file) and os.path.exists(key_file):
        tls_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls_ctx.set_ciphers('ECDHE+AESGCM')
        tls_ctx.load_cert_chain(cert_file, key_file)
        print(f"OnBoarding MQTT mock (TLS) listening on {HOST}:{PORT}", flush=True)
    else:
        print(f"OnBoarding MQTT mock listening on {HOST}:{PORT}", flush=True)

    try:
        while True:
            try:
                cs, addr = srv.accept()
                cs.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                if tls_ctx:
                    try:
                        cs = tls_ctx.wrap_socket(cs, server_side=True)
                    except ssl.SSLError as e:
                        print(f"  TLS handshake failed: {e}", file=sys.stderr)
                        cs.close()
                        continue
                t = threading.Thread(target=handle_client, args=(cs, addr), daemon=True)
                t.start()
            except socket.timeout:
                continue
    except KeyboardInterrupt:
        pass
    finally:
        srv.close()


if __name__ == "__main__":
    main()
