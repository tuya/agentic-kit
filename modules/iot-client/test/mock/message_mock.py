#!/usr/bin/env python3
"""
MQTT mock for iot_client_message tests.

TLS-enabled MQTT broker on 127.0.0.1:11885.
After client subscribes, sends a raw (unencrypted) test message on the
subscribed topic.  Also echoes any PUBLISH back on the subscribe topic
so the C test can verify the pv23 decrypt path.
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
DEFAULT_PORT = 11885

RAW_TEST_MESSAGE = '{"type":"test","payload":"hello_from_mock"}'

# Invalid format message (not valid JSON, not valid encrypted data)
INVALID_FORMAT_MESSAGE = b'\x00\x01\x02\x03\xff\xfe'

# Encrypted with wrong key - pv23 format but with wrong key/data
# This simulates data that looks like encrypted data but can't be decrypted
WRONG_KEY_ENCRYPTED = b'3.3\x00\x00\x00\x20' + b'A' * 32 + b'\x00\x00\x00\x10' + b'B' * 16


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


def build_publish_packet(topic, payload):
    """Build a QoS 0 MQTT 3.1.1 PUBLISH packet."""
    topic_bytes = encode_string(topic)
    if isinstance(payload, str):
        payload = payload.encode("utf-8")
    var_and_payload = topic_bytes + payload
    remaining = encode_remaining_length(len(var_and_payload))
    return bytes([MQTT_PUBLISH]) + remaining + var_and_payload


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
                # MQTT 3.1.1 CONNACK: flags(1) + return_code(1)
                connack = bytes([MQTT_CONNACK, 2, 0, 0])
                client_sock.sendall(connack)

            elif pkt_type == MQTT_SUBSCRIBE:
                pkt_id = struct.unpack(">H", payload[0:2])[0]
                off = 2
                topic_bytes, off = parse_string(payload, off)
                subscribe_topic = topic_bytes.decode("utf-8") if isinstance(topic_bytes, bytes) else topic_bytes

                # MQTT 3.1.1 SUBACK: pkt_id(2) + return_code(1)
                suback = bytes([MQTT_SUBACK, 3]) + struct.pack(">H", pkt_id) + bytes([0])

                # Check environment variable for message type
                msg_type = os.getenv('MESSAGE_MOCK_TYPE', 'raw')
                if msg_type == 'invalid_format':
                    test_msg = build_publish_packet(subscribe_topic, INVALID_FORMAT_MESSAGE)
                    print(f"  Sending invalid format message", flush=True)
                elif msg_type == 'wrong_key_encrypted':
                    test_msg = build_publish_packet(subscribe_topic, WRONG_KEY_ENCRYPTED)
                    print(f"  Sending wrong-key encrypted message", flush=True)
                else:
                    test_msg = build_publish_packet(subscribe_topic, RAW_TEST_MESSAGE)
                    print(f"  Sending raw test message", flush=True)

                client_sock.sendall(suback + test_msg)

            elif pkt_type == MQTT_PUBLISH:
                qos = (header_byte & 0x06) >> 1
                topic_bytes, off = parse_string(payload, 0)
                pkt_id = None
                if qos >= 1:
                    pkt_id = struct.unpack(">H", payload[off:off + 2])[0]
                    off += 2

                msg_data = payload[off:]

                combined = b""
                if qos == 1 and pkt_id is not None:
                    # MQTT 3.1.1 PUBACK: pkt_id(2)
                    combined += bytes([MQTT_PUBACK, 2]) + struct.pack(">H", pkt_id)

                if subscribe_topic:
                    echo_pkt = build_publish_packet(subscribe_topic, msg_data)
                    combined += echo_pkt

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
    port = int(os.getenv('MESSAGE_MOCK_PORT', str(DEFAULT_PORT)))

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, port))
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
        print(f"Message MQTT mock (TLS) listening on {HOST}:{port}", flush=True)
    else:
        print(f"Message MQTT mock listening on {HOST}:{port}", flush=True)

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
