#!/usr/bin/env python3
"""
MQTT Mock Server for ESP32 Testing

Receives encrypted messages from command.c, decrypts them using P2.3 protocol,
and sends back encrypted responses.

Message Format (P2.3):
- AAD: "2.3seq_from" (11 bytes) + null byte (1 byte)
- IV: 12 bytes
- Ciphertext: variable length
- Tag: 16 bytes (GCM authentication tag)

Expected JSON messages:
1. thing.getServerInfo - Request server configuration
2. thing.getAgentToken - Request AI agent token
"""

import json
import socket
import struct
import threading
import ssl
import os
import sys
import hashlib

# Try to import from Cryptodome first (pycryptodome), then fall back to Crypto (pycrypto)
try:
    from Cryptodome.Cipher import AES
    from Cryptodome.Random import get_random_bytes
except ImportError:
    from Crypto.Cipher import AES
    from Crypto.Random import get_random_bytes

# MQTT Control Packet Types
MQTT_CONNECT = 0x10
MQTT_CONNACK = 0x20
MQTT_PUBLISH = 0x30
MQTT_PUBACK = 0x40
MQTT_SUBSCRIBE = 0x80
MQTT_SUBACK = 0x90
MQTT_PINGREQ = 0xC0
MQTT_PINGRESP = 0xD0
MQTT_DISCONNECT = 0xE0

# P2.3 Protocol Constants
P23_AAD = b"2.3seq_from"
P23_AAD_LEN = 11
P23_IV_LEN = 12
P23_TAG_LEN = 16


def load_config(filename='../config/clink.conf'):
    """Load configuration from file"""
    config = {
        'host': '127.0.0.1',
        'port': 8883,
        'client_id': None,
        'sec_key': None,
        'device_id': None,
        'local_key': None
    }

    if not os.path.exists(filename):
        print(f"❌ Configuration file not found: {filename}", file=sys.stderr)
        sys.exit(1)

    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            # Skip comments and empty lines
            if not line or line.startswith('#'):
                continue

            # Parse key=value
            if '=' not in line:
                continue

            key, value = line.split('=', 1)
            key = key.strip()
            value = value.strip()

            if key == 'host':
                config['host'] = value
            elif key == 'port':
                config['port'] = int(value)
            elif key == 'client_id':
                config['client_id'] = value
            elif key == 'sec_key':
                config['sec_key'] = value
            elif key == 'device_id':
                config['device_id'] = value
            elif key == 'local_key':
                if len(value) != 16:
                    print(f"❌ local_key must be exactly 16 bytes, got {len(value)}", file=sys.stderr)
                    sys.exit(1)
                config['local_key'] = value.encode('utf-8')

    # Validate required fields
    required = ['client_id', 'sec_key', 'device_id', 'local_key']
    for field in required:
        if config[field] is None:
            print(f"❌ Missing required configuration: {field}", file=sys.stderr)
            sys.exit(1)

    print(f"✅ Configuration loaded from {filename}")
    return config

# Default encryption key (same as in ctest.c)
DEFAULT_LOCAL_KEY = b"1234567890abcdef"


class P23Crypto:
    """P2.3 protocol encryption/decryption"""

    @staticmethod
    def decrypt(data: bytes, key: bytes) -> dict:
        """Decrypt P2.3 encrypted message"""
        # Verify minimum length: AAD(11) + null(1) + IV(12) + tag(16) = 40
        if len(data) < P23_AAD_LEN + 1 + P23_IV_LEN + P23_TAG_LEN:
            raise ValueError(f"Data too short: {len(data)} bytes")

        # Verify AAD
        if data[:P23_AAD_LEN] != P23_AAD or data[P23_AAD_LEN] != 0:
            raise ValueError("Invalid P2.3 AAD")

        # Extract components
        offset = P23_AAD_LEN + 1
        iv = data[offset:offset + P23_IV_LEN]
        offset += P23_IV_LEN

        ciphertext_and_tag = data[offset:]
        if len(ciphertext_and_tag) < P23_TAG_LEN:
            raise ValueError("No tag found")

        ciphertext = ciphertext_and_tag[:-P23_TAG_LEN]
        tag = ciphertext_and_tag[-P23_TAG_LEN:]

        # Decrypt
        cipher = AES.new(key, AES.MODE_GCM, nonce=iv)
        cipher.update(P23_AAD + b'\x00')  # AAD with null terminator
        plaintext = cipher.decrypt_and_verify(ciphertext, tag)

        # Parse JSON
        return json.loads(plaintext.decode('utf-8'))

    @staticmethod
    def encrypt(data: dict, key: bytes) -> bytes:
        """Encrypt message using P2.3 protocol"""
        # Convert to JSON
        json_str = json.dumps(data, separators=(',', ':'))
        plaintext = json_str.encode('utf-8')

        # Generate random IV
        iv = get_random_bytes(P23_IV_LEN)

        # Encrypt
        cipher = AES.new(key, AES.MODE_GCM, nonce=iv)
        cipher.update(P23_AAD + b'\x00')  # AAD with null terminator
        ciphertext, tag = cipher.encrypt_and_digest(plaintext)

        # Build output: AAD + null + IV + ciphertext + tag
        return P23_AAD + b'\x00' + iv + ciphertext + tag


class MQTTPacket:
    """MQTT packet parser and builder"""

    @staticmethod
    def parse_remaining_length(data: bytes, offset: int) -> tuple:
        """Parse MQTT variable length encoding"""
        multiplier = 1
        value = 0
        index = offset

        while True:
            if index >= len(data):
                return 0, offset

            byte = data[index]
            index += 1
            value += (byte & 0x7F) * multiplier

            if (byte & 0x80) == 0:
                break

            multiplier *= 128
            if multiplier > 128 * 128 * 128:
                return 0, offset

        return value, index

    @staticmethod
    def encode_remaining_length(length: int) -> bytes:
        """Encode length in MQTT variable length format"""
        result = bytearray()
        while True:
            byte = length % 128
            length = length // 128
            if length > 0:
                byte |= 0x80
            result.append(byte)
            if length == 0:
                break
        return bytes(result)

    @staticmethod
    def encode_string(s: str) -> bytes:
        """Encode string with 2-byte length prefix"""
        data = s.encode('utf-8')
        return struct.pack('>H', len(data)) + data

    @staticmethod
    def parse_string(data: bytes, offset: int) -> tuple:
        """Parse string with 2-byte length prefix"""
        if offset + 2 > len(data):
            return "", offset

        length = struct.unpack('>H', data[offset:offset+2])[0]
        offset += 2

        if offset + length > len(data):
            return "", offset

        string = data[offset:offset+length].decode('utf-8')
        return string, offset + length


class MQTTMockServer:
    """Simple MQTT mock server for testing (TLS only)"""

    def __init__(self, host='127.0.0.1', port=8883, key=None, 
                 certfile=None, keyfile=None, client_id=None, sec_key=None, device_id=None):
        self.host = host
        self.port = port
        self.key = key if key else b"1234567890abcdef"
        self.certfile = certfile
        self.keyfile = keyfile
        self.client_id = client_id
        self.sec_key = sec_key
        self.device_id = device_id
        self.server_socket = None
        self.running = False
        self.ssl_context = None

        # Always setup SSL context
        self.setup_ssl_context()

    def setup_ssl_context(self):
        """Setup SSL/TLS context with certificates"""
        self.ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)

        # Load certificate and key
        if self.certfile and self.keyfile:
            if not os.path.exists(self.certfile):
                raise FileNotFoundError(f"Certificate file not found: {self.certfile}")
            if not os.path.exists(self.keyfile):
                raise FileNotFoundError(f"Key file not found: {self.keyfile}")

            self.ssl_context.load_cert_chain(self.certfile, self.keyfile)
            print(f"🔐 Loaded certificate: {self.certfile}")
            print(f"🔑 Loaded private key: {self.keyfile}")
        else:
            # Generate self-signed certificate if not provided
            print("⚠️  No certificate provided, generating self-signed certificate...")
            self.generate_self_signed_cert()

        # Force TLS 1.2 only
        self.ssl_context.minimum_version = ssl.TLSVersion.TLSv1_2
        self.ssl_context.maximum_version = ssl.TLSVersion.TLSv1_2

        # Set cipher suite to ECDHE-ECDSA-AES128-GCM-SHA256
        self.ssl_context.set_ciphers('ECDHE-ECDSA-AES128-GCM-SHA256')
        print(f"🔒 TLS 1.2 configured with ECDHE-ECDSA-AES128-GCM-SHA256")

    def generate_self_signed_cert(self):
        """Generate self-signed certificate for testing"""
        from datetime import datetime, timedelta, timezone
        import ipaddress

        # Use fixed filenames in config directory
        config_dir = os.path.join(os.path.dirname(__file__), '..', 'config')
        os.makedirs(config_dir, exist_ok=True)

        self.certfile = os.path.join(config_dir, 'cert.pem')
        self.keyfile = os.path.join(config_dir, 'key.pem')        # Check if files already exist
        if os.path.exists(self.certfile) and os.path.exists(self.keyfile):
            print(f"✅ Using existing certificate files:")
            print(f"   Cert: {self.certfile}")
            print(f"   Key: {self.keyfile}")
            self.ssl_context.load_cert_chain(self.certfile, self.keyfile)
            return

        try:
            from cryptography import x509
            from cryptography.x509.oid import NameOID
            from cryptography.hazmat.primitives import hashes
            from cryptography.hazmat.primitives.asymmetric import ec
            from cryptography.hazmat.primitives import serialization

            print("🔧 Generating new ECDSA self-signed certificate...")

            # Generate ECDSA private key (P-256 curve for ECDHE-ECDSA)
            private_key = ec.generate_private_key(ec.SECP256R1())

            # Create certificate
            subject = issuer = x509.Name([
                x509.NameAttribute(NameOID.COUNTRY_NAME, "US"),
                x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "CA"),
                x509.NameAttribute(NameOID.LOCALITY_NAME, "San Francisco"),
                x509.NameAttribute(NameOID.ORGANIZATION_NAME, "MQTT Mock Server"),
                x509.NameAttribute(NameOID.COMMON_NAME, self.host),
            ])

            # Build Subject Alternative Names
            san_list = [
                x509.DNSName(self.host),
                x509.DNSName("localhost"),
            ]

            # Add IP address if it's a valid IP
            try:
                if self.host == 'localhost':
                    san_list.append(x509.IPAddress(ipaddress.IPv4Address('127.0.0.1')))
                else:
                    san_list.append(x509.IPAddress(ipaddress.IPv4Address(self.host)))
            except (ValueError, ipaddress.AddressValueError):
                # Not a valid IP address, skip
                pass

            cert = x509.CertificateBuilder().subject_name(
                subject
            ).issuer_name(
                issuer
            ).public_key(
                private_key.public_key()
            ).serial_number(
                x509.random_serial_number()
            ).not_valid_before(
                datetime.now(timezone.utc)
            ).not_valid_after(
                datetime.now(timezone.utc) + timedelta(days=365)
            ).add_extension(
                x509.SubjectAlternativeName(san_list),
                critical=False,
            ).sign(private_key, hashes.SHA256())

            # Write certificate to file
            with open(self.certfile, 'wb') as f:
                f.write(cert.public_bytes(serialization.Encoding.PEM))

            # Write private key to file
            with open(self.keyfile, 'wb') as f:
                f.write(private_key.private_bytes(
                    encoding=serialization.Encoding.PEM,
                    format=serialization.PrivateFormat.PKCS8,
                    encryption_algorithm=serialization.NoEncryption()
                ))

            self.ssl_context.load_cert_chain(self.certfile, self.keyfile)
            print(f"✅ Generated self-signed certificate (valid for 365 days)")
            print(f"   Cert: {self.certfile}")
            print(f"   Key: {self.keyfile}")

        except ImportError:
            print("❌ Error: cryptography package not installed")
            print("   Install with: pip install cryptography")
            raise RuntimeError("Cannot start TLS server without cryptography package")

    def start(self):
        """Start the MQTT server"""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(5)
        self.running = True

        print(f"🚀 MQTTS (TLS) Mock Server started on {self.host}:{self.port}")
        print(f"🔑 Using encryption key: {self.key.hex()}")
        print(f"🔒 TLS/SSL enabled - minimum version: TLS 1.2")
        print(f"📡 Waiting for connections...\n")

        try:
            while self.running:
                client_socket, address = self.server_socket.accept()

                # Wrap with SSL
                try:
                    client_socket = self.ssl_context.wrap_socket(
                        client_socket,
                        server_side=True
                    )
                    cipher_info = client_socket.cipher()
                    print(f"✅ Client connected from {address}")
                    print(f"   Protocol: {cipher_info[1]}")
                    print(f"   Cipher: {cipher_info[0]}")
                except ssl.SSLError as e:
                    print(f"❌ SSL handshake failed with {address}: {e}")
                    client_socket.close()
                    continue

                # Handle client in a new thread
                client_thread = threading.Thread(
                    target=self.handle_client,
                    args=(client_socket, address)
                )
                client_thread.daemon = True
                client_thread.start()
        except KeyboardInterrupt:
            print("\n🛑 Server shutting down...")
        finally:
            self.stop()

    def stop(self):
        """Stop the MQTT server"""
        self.running = False
        if self.server_socket:
            self.server_socket.close()

    def handle_client(self, client_socket, address):
        """Handle a single MQTT client connection"""
        try:
            while self.running:
                # Read fixed header
                header = client_socket.recv(1)
                if not header:
                    break

                # Keep the full header byte (includes QoS, DUP, RETAIN flags)
                header_byte = header[0]
                packet_type = header_byte & 0xF0

                # Read remaining length
                remaining_data = bytearray()
                while True:
                    byte = client_socket.recv(1)
                    if not byte:
                        return
                    remaining_data.append(byte[0])
                    if (byte[0] & 0x80) == 0:
                        break

                # Parse remaining length
                remaining_length, _ = MQTTPacket.parse_remaining_length(
                    bytes(remaining_data), 0
                )

                # Read the rest of the packet
                payload = b''
                if remaining_length > 0:
                    # Read all data in a loop (recv may not return all data at once)
                    bytes_read = 0
                    payload_parts = []
                    while bytes_read < remaining_length:
                        chunk = client_socket.recv(remaining_length - bytes_read)
                        if not chunk:
                            break
                        payload_parts.append(chunk)
                        bytes_read += len(chunk)
                    payload = b''.join(payload_parts)

                # Handle packet (pass full header byte for QoS extraction)
                self.handle_packet(client_socket, header_byte, payload)

        except Exception as e:
            print(f"❌ Error handling client {address}: {e}")
        finally:
            print(f"👋 Client disconnected: {address}")
            client_socket.close()

    def handle_packet(self, client_socket, header_byte, payload):
        """Handle different MQTT packet types"""

        # Extract packet type (upper 4 bits)
        packet_type = header_byte & 0xF0

        if packet_type == MQTT_CONNECT:
            auth_ok = self.handle_connect(client_socket, payload)
            if not auth_ok:
                # Close connection on authentication failure
                client_socket.close()
                return

        elif packet_type == MQTT_PUBLISH:
            # Extract QoS from header_byte (bits 1-2)
            qos = (header_byte & 0x06) >> 1
            self.handle_publish(client_socket, payload, qos)

        elif packet_type == MQTT_SUBSCRIBE:
            self.handle_subscribe(client_socket, payload)

        elif packet_type == MQTT_PINGREQ:
            self.handle_pingreq(client_socket)

        elif packet_type == MQTT_DISCONNECT:
            print("📤 Client sent DISCONNECT")

    def handle_connect(self, client_socket, payload):
        """Handle MQTT CONNECT packet"""
        print("📥 Received CONNECT")

        # Parse CONNECT packet
        offset = 0
        protocol_name, offset = MQTTPacket.parse_string(payload, offset)
        protocol_level = payload[offset] if offset < len(payload) else 0
        offset += 1

        # Parse connect flags
        connect_flags = payload[offset] if offset < len(payload) else 0
        offset += 1

        # Skip keep alive
        offset += 2

        # Parse client ID
        client_id, offset = MQTTPacket.parse_string(payload, offset)

        # Parse username if present (bit 7 of connect flags)
        username = None
        if connect_flags & 0x80:
            username, offset = MQTTPacket.parse_string(payload, offset)

        # Parse password if present (bit 6 of connect flags)
        password = None
        if connect_flags & 0x40:
            password, offset = MQTTPacket.parse_string(payload, offset)

        print(f"   Protocol: {protocol_name} v{protocol_level}")
        print(f"   Client ID: {client_id}")
        if username:
            print(f"   Username: {username}")
        if password:
            print(f"   Password: {password}")

        # Validate authentication
        auth_ok = True
        if self.client_id and client_id != self.client_id:
            print(f"   ❌ Authentication failed: client_id mismatch (expected: {self.client_id})")
            auth_ok = False

        # Validate password using MD5 hash of sec_key
        # Expected password = md5(sec_key)[8:24] (middle 16 hex chars)
        if self.sec_key:
            expected_password = hashlib.md5(self.sec_key.encode('utf-8')).hexdigest()[8:24]
            if password != expected_password:
                print(f"   ❌ Authentication failed: password mismatch")
                print(f"      Expected: {expected_password}")
                print(f"      Got: {password}")
                auth_ok = False

        if not auth_ok:
            # Send CONNACK with error code
            connack = bytes([MQTT_CONNACK, 2, 0, 4])  # Return code=4 (Bad username or password)
            client_socket.send(connack)
            print("📤 Sent CONNACK (authentication failed)")
            return False

        # Send CONNACK (success)
        connack = bytes([MQTT_CONNACK, 2, 0, 0])  # Session present=0, Return code=0
        client_socket.send(connack)
        print("📤 Sent CONNACK (success)")
        return True

    def handle_subscribe(self, client_socket, payload):
        """Handle MQTT SUBSCRIBE packet"""
        print("\n📥 Received SUBSCRIBE")

        # Extract packet ID
        packet_id = struct.unpack('>H', payload[0:2])[0]

        # Parse topic
        topic, offset = MQTTPacket.parse_string(payload, 2)
        qos = payload[offset] if offset < len(payload) else 0
        print(f"   Packet ID: {packet_id}")
        print(f"   Topic: {topic}")
        print(f"   Requested QoS: {qos}")

        # Send SUBACK
        suback = bytes([MQTT_SUBACK, 3]) + struct.pack('>H', packet_id) + bytes([0])
        client_socket.send(suback)
        print(f"📤 Sent SUBACK (packet_id={packet_id})")

    def handle_publish(self, client_socket, payload, qos=0):
        """Handle MQTT PUBLISH packet"""
        print("\n📥 Received PUBLISH")

        # Parse topic
        topic, offset = MQTTPacket.parse_string(payload, 0)
        print(f"   Topic: {topic}")

        # For QoS1/QoS2, skip the 2-byte packet identifier
        packet_id = None
        if qos > 0:
            if offset + 2 <= len(payload):
                packet_id = struct.unpack('>H', payload[offset:offset+2])[0]
                offset += 2
                print(f"   QoS: {qos}, Packet ID: {packet_id}")
            else:
                print("   ❌ Error: Missing packet ID for QoS>0")
                return

        # Extract message payload
        message_data = payload[offset:]
        print(f"   Payload size: {len(message_data)} bytes")
        print(f"   Payload hex (first 40 bytes): {message_data[:40].hex()}")

        try:
            # Decrypt message
            decrypted = P23Crypto.decrypt(message_data, self.key)
            print(f"   Decrypted: {json.dumps(decrypted, indent=2)}")

            # Send PUBACK for QoS1
            if qos == 1 and packet_id is not None:
                puback = bytes([MQTT_PUBACK, 2]) + struct.pack('>H', packet_id)
                client_socket.send(puback)
                print(f"📤 Sent PUBACK (packet_id={packet_id})")

            # Generate response based on method
            response = self.generate_response(decrypted)

            if response:
                # Encrypt response
                encrypted_response = P23Crypto.encrypt(response, self.key)

                response_topic = topic.replace('/out/', '/in/')
                topic_bytes = MQTTPacket.encode_string(response_topic)
                publish_payload = topic_bytes + encrypted_response

                remaining_length = MQTTPacket.encode_remaining_length(len(publish_payload))
                publish_packet = bytes([MQTT_PUBLISH]) + remaining_length + publish_payload

                client_socket.send(publish_packet)
                print(f"📤 Sent response to: {response_topic}")
                print(f"   Response: {json.dumps(response, indent=2)}\n")

        except Exception as e:
            print(f"❌ Error processing message: {e}")
            print(f"   Expected format: P2.3 (AAD: '2.3seq_from' + null + IV + ciphertext + tag)")
            print(f"   Received data might not be P2.3 encrypted")

    def handle_pingreq(self, client_socket):
        """Handle MQTT PINGREQ packet"""
        print("📥 Received PINGREQ")

        # Send PINGRESP
        pingresp = bytes([MQTT_PINGRESP, 0])
        client_socket.send(pingresp)
        print("📤 Sent PINGRESP")

    def generate_response(self, request: dict) -> dict:
        """Generate response based on request method

        Response format matches real server:
        {
            "bizType": "EVENT",
            "data": {
                "data": {
                    "data": {...actual response data...},
                    "success": true,
                    "errorCode": "",
                    "time": timestamp
                },
                "type": "cloudReturnXXX"
            },
            "bizId": "uuid"
        }
        """
        import uuid
        import time

        method = request.get('method')
        device_id = request.get('deviceId')

        # Validate device_id if configured
        if self.device_id and device_id != self.device_id:
            print(f"   ⚠️  Warning: device_id mismatch (expected: {self.device_id}, got: {device_id})")

        current_time_ms = int(time.time() * 1000)

        if method == 'thing.getServerInfo':
            return {
                'bizType': 'EVENT',
                'data': {
                    'data': {
                        'data': {
                            'clientId': '8DLpDLO2l0bN4rXAK/EPmHrUOwfPllgSgBzZTRrmW3A=',
                            'tcpport': 8884,
                            'credential': 'rhgyGhf2qv2KyEe0Yfo/7rjgQSWPsQ2pzbXW+mDIqs8=',
                            'udpportBackup': 8885,
                            'hosts': ['139.196.242.98', '106.14.77.119'],
                            'expire': int(time.time()) + 3600,
                            'derivedIv': 'PQWLUorkh10s56TP',
                            'derivedAlgorithm': 'AES-CBC',
                            'udpport': 10006,
                            'username': 'vdevo175506497864518'
                        },
                        'success': True,
                        'errorCode': '',
                        'time': current_time_ms
                    },
                    'type': 'cloudReturnServerInfo'
                },
                'bizId': str(uuid.uuid4())
            }

        elif method == 'thing.getAgentToken':
            ai_solution_code = request.get('data', {}).get('aiSolutionCode')
            return {
                'bizType': 'EVENT',
                'data': {
                    'data': {
                        'data': {
                            'agentToken': '6c4d2420c7bd7ec84c',
                            'bizConfig': {
                                'sendData': ['audio', 'video', 'text', 'image', 'file'],
                                'revData': ['text', 'audio', 'image', 'video', 'file'],
                                'bizCode': 65537
                            }
                        },
                        'success': True,
                        'errorCode': '',
                        'time': current_time_ms
                    },
                    'type': 'cloudReturnAgentToken'
                },
                'bizId': str(uuid.uuid4())
            }

        else:
            return {
                'bizType': 'EVENT',
                'data': {
                    'data': {
                        'data': {},
                        'success': False,
                        'errorCode': 'UNKNOWN_METHOD',
                        'time': current_time_ms
                    },
                    'type': 'cloudReturnError'
                },
                'bizId': str(uuid.uuid4())
            }



def main():
    """Main entry point"""
    import argparse

    parser = argparse.ArgumentParser(
        description='MQTTS (TLS) Mock Server for ESP32',
        epilog='Note: This server only supports TLS/SSL connections'
    )
    parser.add_argument('--config', default='../config/clink.conf',
                       help='Path to configuration file (default: ../config/clink.conf)')
    parser.add_argument('--cert', help='Path to certificate file (PEM format, overrides config)')
    parser.add_argument('--key-file', dest='keyfile', help='Path to private key file (PEM format, overrides config)')

    args = parser.parse_args()

    # Load configuration
    config = load_config(args.config)

    print(f"🔧 Configuration:")
    print(f"   Host: {config['host']}")
    print(f"   Port: {config['port']}")
    print(f"   Client ID: {config['client_id']}")
    print(f"   Device ID: {config['device_id']}")
    print(f"   Encryption Key: {config['local_key'].hex()}")

    # Determine certificate paths
    cert_path = args.cert if args.cert else '../config/cert.pem'
    key_path = args.keyfile if args.keyfile else '../config/key.pem'

    # Start server
    try:
        server = MQTTMockServer(
            host=config['host'],
            port=config['port'],
            key=config['local_key'],
            certfile=cert_path,
            keyfile=key_path,
            client_id=config['client_id'],
            sec_key=config['sec_key'],
            device_id=config['device_id']
        )
        server.start()
    except Exception as e:
        print(f"❌ Failed to start server: {e}")
        return 1

    return 0

if __name__ == '__main__':
    import sys
    sys.exit(main())
