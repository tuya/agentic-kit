#!/usr/bin/env python3
"""
Minimal MQTT 3.1.1 broker mock for mqtt_test.c unit tests.

Supports plain TCP and TLS. Handles CONNECT, SUBSCRIBE, PUBLISH (echo back),
PINGREQ, and DISCONNECT.

Environment variables:
  MQTT_MOCK_USE_TLS=1  - Enable TLS (uses test/config/cert.pem and key.pem)
  MQTT_MOCK_PORT=PORT  - Override default port (11883 for TCP, 18883 for TLS)
"""

import socket
import struct
import threading
import ssl
import os
import sys

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
PORT_TCP = 11883
PORT_TLS = 18883

EXPECTED_PASSWORD = "test_pass"


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
    # Protocol Name
    _proto_name, off = parse_string(payload, off)
    # Protocol Level (1 byte) + Connect Flags (1 byte)
    connect_flags = payload[off + 1]
    off += 2
    # Keep Alive (2 bytes)
    off += 2
    has_username = bool(connect_flags & 0x80)
    has_password = bool(connect_flags & 0x40)
    has_will = bool(connect_flags & 0x04)
    # Client ID
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


def handle_client(client_sock, addr):
    subscribe_topic = None

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
                if password != EXPECTED_PASSWORD:
                    print(f"  AUTH FAIL: client={client_id} password={password!r}", flush=True)
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
                # Reject topics starting with "fail/"
                if subscribe_topic.startswith("fail/"):
                    print(f"  SUBSCRIBE FAIL: topic={subscribe_topic}", flush=True)
                    # MQTT 3.1.1 SUBACK: pkt_id(2) + return_code(1), code 0x80 = failure
                    suback = bytes([MQTT_SUBACK, 3]) + struct.pack(">H", pkt_id) + bytes([0x80])
                    client_sock.sendall(suback)
                else:
                    print(f"  SUBSCRIBE OK: topic={subscribe_topic}", flush=True)
                    # MQTT 3.1.1 SUBACK: pkt_id(2) + return_code(1)
                    suback = bytes([MQTT_SUBACK, 3]) + struct.pack(">H", pkt_id) + bytes([0])
                    client_sock.sendall(suback)

            elif pkt_type == MQTT_PUBLISH:
                qos = (header_byte & 0x06) >> 1
                topic_bytes, off = parse_string(payload, 0)
                topic_str = topic_bytes.decode("utf-8") if isinstance(topic_bytes, bytes) else topic_bytes

                pkt_id = None
                if qos >= 1:
                    pkt_id = struct.unpack(">H", payload[off:off + 2])[0]
                    off += 2

                msg_data = payload[off:]

                combined = b""

                if qos == 1 and pkt_id is not None:
                    # MQTT 3.1.1 PUBACK: pkt_id(2)
                    combined += bytes([MQTT_PUBACK, 2]) + struct.pack(">H", pkt_id)

                # Echo the message back on the subscribe topic (QoS 0)
                if subscribe_topic:
                    echo_topic = encode_string(subscribe_topic)
                    var_hdr_payload = echo_topic + msg_data
                    echo_remaining = encode_remaining_length(len(var_hdr_payload))
                    combined += bytes([MQTT_PUBLISH]) + echo_remaining + var_hdr_payload

                if combined:
                    client_sock.sendall(combined)

            elif pkt_type == MQTT_PINGREQ:
                client_sock.sendall(bytes([MQTT_PINGRESP, 0]))

            elif pkt_type == MQTT_DISCONNECT:
                break

    except (ConnectionResetError, BrokenPipeError, OSError):
        pass
    finally:
        client_sock.close()


def main():
    use_tls = os.getenv('MQTT_MOCK_USE_TLS', '').lower() in ('1', 'true', 'yes')
    default_port = PORT_TLS if use_tls else PORT_TCP
    port = int(os.getenv('MQTT_MOCK_PORT', str(default_port)))

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, port))
    srv.listen(5)
    srv.settimeout(1.0)

    ssl_context = None
    if use_tls:
        config_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'config')
        cert_file = os.path.join(config_dir, 'cert.pem')
        key_file = os.path.join(config_dir, 'key.pem')
        if os.path.exists(cert_file) and os.path.exists(key_file):
            ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ssl_context.load_cert_chain(cert_file, key_file)
            print(f"MQTT test mock (TLS) listening on {HOST}:{port}", flush=True)
        else:
            print(f"TLS requested but cert/key not found, falling back to TCP", flush=True)
            use_tls = False

    if not use_tls:
        print(f"MQTT test mock listening on {HOST}:{port}", flush=True)

    try:
        while True:
            try:
                cs, addr = srv.accept()
                cs.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                if ssl_context:
                    try:
                        cs = ssl_context.wrap_socket(cs, server_side=True)
                    except ssl.SSLError as e:
                        print(f"  TLS handshake failed: {e}", flush=True)
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
