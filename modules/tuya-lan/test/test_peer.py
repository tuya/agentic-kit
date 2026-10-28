"""Independent LAN 3.5 peer against the real socket service and IoT DP registry.

Uses Python/PyCryptodome, not the production frame encoder or protocol helpers.
All sockets are loopback with ephemeral ports. No cloud credentials are needed.
"""
import hashlib
import hmac
import json
import os
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from Crypto.Cipher import AES

KEY = b"0123456789abcdef"
DISCOVERY_KEY = hashlib.md5(b"yGAdlopoPVldABfn").digest()
HEAD = struct.Struct("!IHIII")


def encode(key, sequence, command, payload):
    header = HEAD.pack(0x6699, 0, sequence, command, 28 + len(payload))
    nonce = os.urandom(12)
    cipher = AES.new(key, AES.MODE_GCM, nonce=nonce)
    cipher.update(header[4:])
    encrypted, tag = cipher.encrypt_and_digest(payload)
    return header + nonce + encrypted + tag + b"\0\0\x99\x66"


def decode(key, packet):
    magic, reserved, seq, command, length = HEAD.unpack(packet[:18])
    assert magic == 0x6699 and reserved == 0 and len(packet) == 22 + length
    assert packet[-4:] == b"\0\0\x99\x66" and 28 <= length <= 4096
    cipher = AES.new(key, AES.MODE_GCM, nonce=packet[18:30])
    cipher.update(packet[4:18])
    return seq, command, cipher.decrypt_and_verify(packet[30:-20], packet[-20:-4])


def read_exact(sock, length):
    result = bytearray()
    while len(result) < length:
        data = sock.recv(length - len(result))
        if not data:
            raise AssertionError("unexpected EOF")
        result.extend(data)
    return bytes(result)


class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=2)
        self.socket.settimeout(2)
        self.sequence = 0
        self.key = KEY

    def close(self):
        self.socket.close()

    def packet(self, command, data=b""):
        self.sequence += 1
        return encode(self.key, self.sequence, command, data)

    def send(self, command, data=b""):
        self.socket.sendall(self.packet(command, data))

    def receive(self):
        header = read_exact(self.socket, 18)
        length = HEAD.unpack(header)[4]
        assert 28 <= length <= 4096
        _, command, payload = decode(self.key, header + read_exact(self.socket, length + 4))
        assert len(payload) >= 4
        return command, struct.unpack("!I", payload[:4])[0], payload[4:]

    def handshake(self, fragment=False):
        rand_a = os.urandom(16)
        request = self.packet(3, rand_a)
        if fragment:
            for chunk in (request[:2], request[2:17], request[17:31], request[31:]):
                self.socket.sendall(chunk)
        else:
            self.socket.sendall(request)
        command, ret, response = self.receive()
        assert command == 4 and ret == 0 and len(response) == 48
        rand_b = response[:16]
        assert hmac.compare_digest(response[16:], hmac.digest(KEY, rand_a, "sha256"))
        self.send(5, hmac.digest(KEY, rand_b, "sha256"))
        cipher = AES.new(KEY, AES.MODE_GCM, nonce=rand_a[:12])
        self.key = cipher.encrypt(bytes(a ^ b for a, b in zip(rand_a, rand_b)))

    def query(self):
        self.send(0x10, b"{}")
        command, ret, data = self.receive()
        assert command == 0x10 and ret == 0
        return json.loads(data)


class LANPeerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.logs = tempfile.TemporaryFile(mode="w+b")
        cls.addClassCleanup(cls.logs.close)
        env = dict(os.environ, TUYA_DEVICE_ID="lan-wire-test-device",
                   TUYA_LOCAL_KEY=KEY.decode(), TUYA_UUID="0123456789abcdef",
                   TUYA_PRODUCT_KEY="fedcba9876543210")
        cls.device = subprocess.Popen([sys.argv[1], "127.0.0.1", "0", "0"],
                                      env=env, stdout=cls.logs, stderr=cls.logs)
        cls.addClassCleanup(cls.shutdown)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            cls.logs.seek(0)
            logs = cls.logs.read().decode(errors="replace")
            match = re.search(r"LAN listening: TCP (\d+), UDP (\d+)", logs)
            if match:
                cls.tcp_port, cls.udp_port = map(int, match.groups())
                return
            if cls.device.poll() is not None:
                raise AssertionError("device exited during startup:\n" + logs)
            time.sleep(0.01)
        raise AssertionError("device never started:\n" + logs)

    @classmethod
    def shutdown(cls):
        if cls.device.poll() is None:
            cls.device.terminate()
            try:
                cls.device.wait(timeout=5)
            except subprocess.TimeoutExpired:
                cls.device.kill()
                cls.device.wait(timeout=5)

    def peer(self):
        peer = Peer(self.tcp_port)
        self.addCleanup(peer.close)
        return peer

    def assert_closed(self, peer):
        try:
            self.assertEqual(peer.socket.recv(1), b"")
        except ConnectionResetError:
            pass

    def test_control_updates_shared_dp_and_explicit_report(self):
        peer = self.peer()
        peer.handshake()
        body = json.dumps({"protocol": 5, "data": {"dps": {"1": True}}}).encode()
        peer.send(0x0D, b"3.5" + b"\0" * 12 + body)
        frames = [peer.receive(), peer.receive()]
        ack = next(frame for frame in frames if frame[0] == 0x0D)
        report = next(frame for frame in frames if frame[0] == 0x08)
        self.assertEqual(ack[1], 0)
        self.assertEqual(report[1], 0)
        self.assertEqual(report[2][:3], b"3.5")
        envelope = json.loads(report[2][15:])
        self.assertEqual(envelope["data"]["dps"], {"1": True})
        self.assertEqual(peer.query()["dps"], {"1": True})

    def test_invalid_batch_does_not_change_dp(self):
        peer = self.peer()
        peer.handshake()
        before = peer.query()
        body = b'{"data":{"dps":{"1":false,"255":1}}}'
        peer.send(7, b"3.5" + b"\0" * 12 + body)
        command, ret, _ = peer.receive()
        self.assertEqual(command, 7)
        self.assertNotEqual(ret, 0)
        self.assertEqual(peer.query(), before)

    def test_fragmented_handshake_and_coalesced_messages(self):
        peer = self.peer()
        peer.handshake(fragment=True)
        peer.socket.sendall(peer.packet(9) + peer.packet(0x10, b"{}"))
        first, second = peer.receive(), peer.receive()
        self.assertEqual((first[0], first[1], second[0], second[1]), (9, 0, 0x10, 0))

    def test_escaped_nul_cannot_be_normalized_into_dp_id(self):
        peer = self.peer()
        peer.handshake()
        before = peer.query()
        peer.send(0x0D, b"3.5" + b"\0" * 12 + b'{"data":{"dps":{"1\\u0000x":true}}}')
        command, ret, _ = peer.receive()
        self.assertEqual(command, 0x0D)
        self.assertNotEqual(ret, 0)
        self.assertEqual(peer.query(), before)

    def test_query_unicode_lexical_contract(self):
        peer = self.peer()
        peer.handshake()
        before = peer.query()
        # Query metadata exercises authenticated JSON without requiring a string
        # DP in the demo's product schema. Malformed Unicode must be rejected
        # before the application callback; the Session remains usable.
        invalid = (
            b"\xc0\x80", b"\xe0\x9f\xbf", b"\xf0\x8f\xbf\xbf",  # overlong
            b"\xed\xa0\x80", b"\xed\xbf\xbf",  # UTF-16 surrogate range
            b"\xf4\x90\x80\x80", b"\xf5\x80\x80\x80",  # above U+10FFFF
            b"\xe1\x80x", b"\x80", b"\xf0\x90\x80",  # bad/truncated tails
            b"\\uD800", b"\\uDC00", b"\\uD800\\u0041", b"\\u0000",
        )
        for value in invalid:
            with self.subTest(invalid=value):
                peer.send(0x10, b'{"note":"' + value + b'"}')
                command, ret, _ = peer.receive()
                self.assertEqual(command, 0x10)
                self.assertNotEqual(ret, 0)
                self.assertEqual(peer.query(), before)
        valid = (
            "¢".encode(), "中".encode(), "😀".encode(),
            b"\xed\x9f\xbf", b"\xf4\x8f\xbf\xbf",  # scalar boundaries
            b"\\uD83D\\uDE00", b"\\\\u0000",  # pair and literal escape text
        )
        for value in valid:
            with self.subTest(valid=value):
                peer.send(0x10, b'{"note":"' + value + b'"}')
                command, ret, data = peer.receive()
                self.assertEqual((command, ret), (0x10, 0))
                self.assertEqual(json.loads(data), before)

    def test_control_requires_handshake(self):
        peer = self.peer()
        peer.send(9)
        self.assert_closed(peer)

    def test_bad_tag_closes_only_its_session(self):
        bad, good = self.peer(), self.peer()
        bad.handshake()
        good.handshake()
        frame = bytearray(bad.packet(9))
        frame[-5] ^= 1
        bad.socket.sendall(frame)
        self.assert_closed(bad)
        self.assertIn("dps", good.query())

    def test_replayed_frame_closes_session(self):
        peer = self.peer()
        peer.handshake()
        packet = peer.packet(9)
        peer.socket.sendall(packet)
        self.assertEqual(peer.receive()[:2], (9, 0))
        peer.socket.sendall(packet)
        self.assert_closed(peer)

    def test_oversized_frame_is_rejected_before_body(self):
        peer = self.peer()
        peer.socket.sendall(HEAD.pack(0x6699, 0, 1, 3, 0x7FFFFFFF))
        self.assert_closed(peer)

    def test_udp_discovery_uses_real_source(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(2)
            request = encode(DISCOVERY_KEY, 1, 0x25, b'{"ip":"127.0.0.1","from":"app"}')
            udp.sendto(request, ("127.0.0.1", self.udp_port))
            response, _ = udp.recvfrom(4096)
            _, command, data = decode(DISCOVERY_KEY, response)
            self.assertEqual(command, 0x23)
            self.assertEqual(data[:4], b"\0" * 4)
            discovery = json.loads(data[4:])
            self.assertEqual(discovery["gwId"], "lan-wire-test-device")
            self.assertEqual(discovery["version"], "3.5")
            self.assertTrue(discovery["encrypt"])


if __name__ == "__main__":
    # Keep the executable path out of unittest's argument parser.
    unittest.main(argv=[sys.argv[0]], verbosity=2)
