#!/usr/bin/env python3
"""
ATOP Mock Server for ESP32 Testing

Simulates Tuya ATOP cloud service for testing device activation and AI configuration.

ATOP Protocol:
- HTTP POST to /d.json
- URL parameters: a (API), devId, et, t (timestamp), uuid, v (version), sign (MD5)
- Request body: data=HEX_ENCODED_ENCRYPTED_JSON (AES-128-GCM encrypted)
- Response body: JSON with base64-encoded encrypted data in "result" field
- Encryption key: authkey (first 16 bytes for AES-128-GCM) or sec_key for AI config

Usage:
    # Start mock server (default: https://127.0.0.1:443 for HTTPS, http://127.0.0.1:80 for HTTP)
    cd test/mock
    python3 atop_mock.py
    
    # Configure client to use mock server
    # In test/config/clink.conf, set:
    #   host=127.0.0.1
    #   port=443  # HTTPS (use 80 for HTTP)
    
    # For HTTPS (optional):
    #   export ATOP_MOCK_USE_SSL=1
    #   export ATOP_MOCK_CERT=cert.pem
    #   export ATOP_MOCK_KEY=key.pem
    #   python3 atop_mock.py

Supported APIs:
    - thing.device.opensdk.active: Device activation
    - thing.ai.agent.token.get: AI token retrieval
    - tuya.device.qrcode.info.get: QR code info retrieval
    - tuya.device.meta.save: Device metadata save
"""

import json
import socket
import threading
import ssl
import os
import sys
import hashlib
import urllib.parse
import time
import base64
from http.server import HTTPServer, BaseHTTPRequestHandler


class ReusableHTTPServer(HTTPServer):
    allow_reuse_address = True

# Try to import from Cryptodome first (pycryptodome), then fall back to Crypto (pycrypto)
try:
    from Cryptodome.Cipher import AES
    from Cryptodome.Random import get_random_bytes
except ImportError:
    from Crypto.Cipher import AES
    from Crypto.Random import get_random_bytes

# AES-GCM constants
AES_GCM128_NONCE_LEN = 12
AES_GCM128_TAG_LEN = 16
AES_GCM128_KEY_LEN = 16


def load_config(filename=None):
    """Load configuration from file (path relative to this script)"""
    if filename is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        filename = os.path.join(script_dir, '..', 'config', 'atop.conf')
    config = {
        'host': '127.0.0.1',
        'port': 8443,  # HTTPS default port (use 80 for HTTP)
        'uuid': None,
        'authkey': None,
        'device_id': None,
        'sec_key': None
    }

    if not os.path.exists(filename):
        print(f"⚠️  Configuration file not found: {filename}", file=sys.stderr)
        return config

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
                try:
                    config['port'] = int(value)
                except ValueError:
                    pass
            elif key == 'uuid':
                config['uuid'] = value
            elif key == 'authkey':
                config['authkey'] = value
            elif key == 'device_id':
                config['device_id'] = value
            elif key == 'sec_key':
                config['sec_key'] = value

    return config


def md5_sign(params, key):
    """Generate MD5 signature for URL parameters
    
    Format: key1=value1||key2=value2||...||key
    """
    # Sort parameters by key for consistent signing
    sorted_params = sorted(params.items())
    
    # Build signature string
    sign_str = ''
    for k, v in sorted_params:
        sign_str += f'{k}={v}||'
    sign_str += key
    
    # Calculate MD5
    md5_hash = hashlib.md5(sign_str.encode('utf-8')).hexdigest()
    return md5_hash.lower()


def verify_signature(url_params, key):
    """Verify URL parameter signature"""
    if 'sign' not in url_params:
        return False
    
    sign = url_params.pop('sign')
    
    # Generate expected signature
    expected_sign = md5_sign(url_params, key)
    
    return sign.lower() == expected_sign.lower()


def aes_gcm_decrypt(key, encrypted_data_hex):
    """Decrypt AES-128-GCM encrypted data (hex encoded)"""
    try:
        # Convert hex string to bytes
        encrypted_data = bytes.fromhex(encrypted_data_hex)
        
        if len(encrypted_data) < AES_GCM128_NONCE_LEN + AES_GCM128_TAG_LEN:
            return None
        
        # Extract nonce, ciphertext, and tag
        nonce = encrypted_data[:AES_GCM128_NONCE_LEN]
        ciphertext = encrypted_data[AES_GCM128_NONCE_LEN:-AES_GCM128_TAG_LEN]
        tag = encrypted_data[-AES_GCM128_TAG_LEN:]
        
        # Ensure key is 16 bytes
        if len(key) > AES_GCM128_KEY_LEN:
            key = key[:AES_GCM128_KEY_LEN]
        elif len(key) < AES_GCM128_KEY_LEN:
            key = key.ljust(AES_GCM128_KEY_LEN, b'\0')
        
        # Decrypt
        cipher = AES.new(key, AES.MODE_GCM, nonce=nonce)
        plaintext = cipher.decrypt_and_verify(ciphertext, tag)
        
        return plaintext.decode('utf-8')
    except Exception as e:
        print(f"❌ Decryption error: {e}", file=sys.stderr)
        return None


def aes_gcm_encrypt(key, plaintext):
    """Encrypt data using AES-128-GCM"""
    try:
        # Ensure key is 16 bytes
        if len(key) > AES_GCM128_KEY_LEN:
            key = key[:AES_GCM128_KEY_LEN]
        elif len(key) < AES_GCM128_KEY_LEN:
            key = key.ljust(AES_GCM128_KEY_LEN, b'\0')
        
        # Generate random nonce
        nonce = get_random_bytes(AES_GCM128_NONCE_LEN)
        
        # Encrypt
        cipher = AES.new(key, AES.MODE_GCM, nonce=nonce)
        ciphertext, tag = cipher.encrypt_and_digest(plaintext.encode('utf-8'))
        
        # Combine nonce + ciphertext + tag
        encrypted = nonce + ciphertext + tag
        
        # Return as hex string
        return encrypted.hex().upper()
    except Exception as e:
        print(f"❌ Encryption error: {e}", file=sys.stderr)
        return None


def handle_activate_request(request_data, config):
    """Handle device activation request"""
    try:
        request_json = json.loads(request_data)
        
        # Extract request parameters
        token = request_json.get('token', '')
        sw_ver = request_json.get('swVer', '')
        product_key = request_json.get('productKey', '')
        pv = request_json.get('pv', '')
        bv = request_json.get('bv', '')
        
        # Generate mock response
        # In real scenario, this would validate credentials and generate device ID
        device_id = config.get('device_id') or f"mock_device_{int(time.time())}"
        sec_key = config.get('sec_key') or "mock_sec_key_1234567890abcdef"
        
        response = {
            "success": True,
            "t": int(time.time()),
            "result": {
                "devId": device_id,
                "secKey": sec_key,
                "name": "Mock Device",
                "productKey": product_key or "mock_product_key",
                "localKey": "1234567890abcdef",
                "schemaId": "mock_schema_id",
                "schema": [],
                "timezoneId": "Asia/Shanghai",
                "ownerId": "mock_owner_id",
                "nodeId": "mock_node_id",
                "icon": "",
                "ip": "192.168.1.100",
                "mac": "AA:BB:CC:DD:EE:FF",
                "uuid": config.get('uuid', 'mock_uuid'),
                "pv": pv or "2.0",
                "bv": bv or "1.0",
                "verSw": sw_ver or "1.0.0",
                "cdVer": "1.0.0",
                "cadVer": "1.0.3"
            }
        }
        
        return json.dumps(response, separators=(',', ':'))
    except Exception as e:
        print(f"❌ Error handling activate request: {e}", file=sys.stderr)
        response = {
            "success": False,
            "t": int(time.time()),
            "errorCode": "ACTIVATE_FAILED",
            "errorMsg": str(e)
        }
        return json.dumps(response, separators=(',', ':'))


def handle_ai_token_request(request_data, config):
    """Handle AI token request"""
    try:
        request_json = json.loads(request_data)

        agent_code = request_json.get('agentCode', '')

        device_id = config.get('device_id', 'device')
        response = {
            "success": True,
            "t": int(time.time()),
            "result": {
                "connect_conf": {
                    "derived_iv": "mock_iv_12345678",
                    "tcpport": 8884,
                    "udpport_backup": 443,
                    "credential": "mock_credential_abcdef",
                    "derived_client_id": f"mock_client_{device_id}",
                    "hosts": ["127.0.0.1"],
                    "expire": int(time.time()) + 86400,
                    "domains": ["rtc-1-cn.mock.com"],
                    "derived_algorithm": "AES-CBC",
                    "udpport": 10006,
                    "username": f"mock_user_{device_id}"
                },
                "session_conf": {
                    "agentToken": f"mock_token_{agent_code}",
                    "bizConfig": {
                        "bizCode": 65537,
                        "revData": ["text", "audio", "image", "video", "file"],
                        "sendData": ["text", "audio", "image", "video", "file"]
                    }
                }
            }
        }

        return json.dumps(response, separators=(',', ':'))
    except Exception as e:
        print(f"❌ Error handling AI token request: {e}", file=sys.stderr)
        response = {
            "success": False,
            "t": int(time.time()),
            "errorCode": "AI_TOKEN_FAILED",
            "errorMsg": str(e)
        }
        return json.dumps(response, separators=(',', ':'))


def handle_device_meta_save_request(request_data, config):
    """Handle device meta save request (tuya.device.meta.save)"""
    try:
        request_json = json.loads(request_data)

        metas = request_json.get('metas', '')
        if not metas:
            return json.dumps({
                "success": False,
                "t": int(time.time()),
                "errorCode": "ILLEGAL_PARAM",
                "errorMsg": "missing metas"
            }, separators=(',', ':'))

        response = {
            "success": True,
            "t": int(time.time()),
            "result": True
        }

        return json.dumps(response, separators=(',', ':'))
    except Exception as e:
        print(f"❌ Error handling device meta save request: {e}", file=sys.stderr)
        response = {
            "success": False,
            "t": int(time.time()),
            "errorCode": "META_SAVE_FAILED",
            "errorMsg": str(e)
        }
        return json.dumps(response, separators=(',', ':'))


def handle_schema_newest_get(request_data, config):
    """Handle newest-schema query (tuya.device.schema.newest.get).

    Returns an empty schema ([] = no update) when the request 'version' equals
    the sentinel "NOUPDATE"; otherwise returns a fresh schema array (update).
    """
    try:
        request_json = json.loads(request_data)
        version = request_json.get('version', '')

        if version == 'NOUPDATE':
            result = []
        else:
            result = [
                {"id": 1, "type": "bool",   "mode": "rw"},
                {"id": 2, "type": "value",  "mode": "rw", "property": {"min": 0, "max": 1000}},
                {"id": 3, "type": "enum",   "mode": "rw", "property": {"range": ["white", "warm", "cold"]}},
                {"id": 7, "type": "string", "mode": "rw", "property": {"maxlen": 32}}
            ]

        response = {
            "success": True,
            "t": int(time.time()),
            "result": result
        }
        return json.dumps(response, separators=(',', ':'))
    except Exception as e:
        print(f"❌ Error handling schema newest get request: {e}", file=sys.stderr)
        return json.dumps({
            "success": False,
            "t": int(time.time()),
            "errorCode": "SCHEMA_NEWEST_FAILED",
            "errorMsg": str(e)
        }, separators=(',', ':'))


def handle_qrcode_info_request(request_data, config, url_params=None):
    """Handle QR code info request (tuya.device.qrcode.info.get)"""
    try:
        url_params = url_params or {}
        t = url_params.get('t', '')

        if not t:
            return json.dumps({
                "success": False,
                "t": int(time.time()),
                "errorCode": "SING_VALIDATE_FALED",
                "errorMsg": "missing t"
            }, separators=(',', ':'))

        request_json = json.loads(request_data)

        if 't' not in request_json:
            return json.dumps({
                "success": False,
                "t": int(time.time()),
                "errorCode": "ILLEGAL_PARAM",
                "errorMsg": "missing t in request body"
            }, separators=(',', ':'))

        app_id = request_json.get('appId', '')
        qr_type = request_json.get('type', 0)

        response = {
            "success": True,
            "t": int(time.time()),
            "result": {"shortUrl": f"https://smartapp.tuya.com/s/p?p=mock_qrcode_{app_id}_{qr_type}"}
        }

        return json.dumps(response, separators=(',', ':'))
    except Exception as e:
        print(f"❌ Error handling qrcode info request: {e}", file=sys.stderr)
        response = {
            "success": False,
            "t": int(time.time()),
            "errorCode": "QRCODE_INFO_FAILED",
            "errorMsg": str(e)
        }
        return json.dumps(response, separators=(',', ':'))


class ATOPMockHandler(BaseHTTPRequestHandler):
    """HTTP request handler for ATOP mock server"""
    
    def __init__(self, *args, config=None, **kwargs):
        self.config = config or {}
        super().__init__(*args, **kwargs)
    
    def log_message(self, format, *args):
        """Override to use custom logging"""
        print(f"📡 {self.address_string()} - {format % args}")
    
    def do_POST(self):
        """Handle POST requests"""
        try:
            # Parse URL and query parameters
            parsed_path = urllib.parse.urlparse(self.path)
            if parsed_path.path != '/d.json':
                self.send_error(404, "Not Found")
                return
            
            query_params = urllib.parse.parse_qs(parsed_path.query)
            # Convert list values to single values
            url_params = {k: v[0] if isinstance(v, list) and len(v) > 0 else v 
                         for k, v in query_params.items()}
            
            # Extract API name
            api = url_params.get('a', '')
            uuid = url_params.get('uuid', '')
            devid = url_params.get('devId', '')
            
            print(f"📥 Received ATOP request:")
            print(f"   API: {api}")
            print(f"   UUID: {uuid}")
            print(f"   Device ID: {devid}")
            
            # Determine encryption key
            # For activation: use authkey from config
            # For AI config: use sec_key from config (or devid's key)
            if api == 'thing.device.opensdk.active':
                key = self.config.get('authkey', '').encode('utf-8')
            elif api == 'thing.ai.agent.token.get':
                key = self.config.get('sec_key', '').encode('utf-8')
            elif api == 'tuya.device.qrcode.info.get':
                key = self.config.get('authkey', '').encode('utf-8')
            elif api == 'tuya.device.meta.save':
                key = self.config.get('sec_key', '').encode('utf-8')
            elif api == 'tuya.device.schema.newest.get':
                key = self.config.get('sec_key', '').encode('utf-8')
            else:
                key = self.config.get('authkey', '').encode('utf-8')
            
            # Verify signature (optional for mock, but good to test)
            # Note: We need the key to verify, but we'll skip strict verification for mock
            # if not verify_signature(url_params.copy(), key.decode('utf-8')):
            #     print(f"⚠️  Signature verification failed (skipped in mock)")
            
            # Read request body
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length).decode('utf-8')
            
            # Parse request body (format: data=HEX_STRING)
            if not body.startswith('data='):
                self.send_error(400, "Bad Request: Invalid data format")
                return
            
            encrypted_data_hex = body[5:]  # Skip "data="
            
            # Decrypt request
            decrypted_data = aes_gcm_decrypt(key, encrypted_data_hex)
            if decrypted_data is None:
                self.send_error(400, "Bad Request: Decryption failed")
                return
            
            print(f"📝 Decrypted request: {decrypted_data}")
            
            # Handle different APIs
            if api == 'thing.device.opensdk.active':
                response_json = handle_activate_request(decrypted_data, self.config)
            elif api == 'thing.ai.agent.token.get':
                response_json = handle_ai_token_request(decrypted_data, self.config)
            elif api == 'tuya.device.qrcode.info.get':
                response_json = handle_qrcode_info_request(decrypted_data, self.config, url_params)
            elif api == 'tuya.device.meta.save':
                response_json = handle_device_meta_save_request(decrypted_data, self.config)
            elif api == 'tuya.device.schema.newest.get':
                response_json = handle_schema_newest_get(decrypted_data, self.config)
            else:
                response_json = json.dumps({
                    "success": False,
                    "t": int(time.time()),
                    "errorCode": "UNKNOWN_API",
                    "errorMsg": f"Unknown API: {api}"
                }, separators=(',', ':'))
            
            print(f"📤 Response JSON: {response_json}")
            
            # Encrypt response using AES-128-GCM
            encrypted_response_hex = aes_gcm_encrypt(key, response_json)
            if encrypted_response_hex is None:
                self.send_error(500, "Internal Server Error: Encryption failed")
                return
            
            # Convert hex to bytes, then base64 encode
            encrypted_response_bytes = bytes.fromhex(encrypted_response_hex)
            encrypted_response_b64 = base64.b64encode(encrypted_response_bytes).decode('utf-8')
            
            # Create response JSON with base64-encoded encrypted data
            response_obj = {
                "result": encrypted_response_b64
            }
            response_body = json.dumps(response_obj, separators=(',', ':'))
            
            print(f"📤 Encrypted response (base64): {encrypted_response_b64[:64]}...")
            
            # Send response
            self.send_response(200)
            self.send_header('Content-Type', 'application/json;charset=UTF-8')
            self.send_header('Content-Length', str(len(response_body)))
            self.end_headers()
            self.wfile.write(response_body.encode('utf-8'))
            
        except Exception as e:
            print(f"❌ Error handling request: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            self.send_error(500, f"Internal Server Error: {str(e)}")
    
    def do_GET(self):
        """Handle GET requests (health check)"""
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.end_headers()
        self.wfile.write(b'ATOP Mock Server is running\n')
    
    def do_OPTIONS(self):
        """Handle OPTIONS requests (CORS)"""
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()


def create_handler(config):
    """Create handler class with config"""
    class Handler(ATOPMockHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, config=config, **kwargs)
    return Handler


def main():
    """Main entry point"""
    print("╔════════════════════════════════════════════════════════════╗")
    print("║              ATOP Mock Server for ESP32 Testing            ║")
    print("╚════════════════════════════════════════════════════════════╝")
    print()
    
    # Load configuration
    config = load_config()
    
    host = config.get('host', '127.0.0.1')
    port = int(os.getenv('ATOP_MOCK_PORT', str(config.get('port', 443))))
    
    print(f"📋 Configuration:")
    print(f"   Host: {host}")
    print(f"   Port: {port}")
    print(f"   UUID: {config.get('uuid', '(not set)')}")
    print(f"   AuthKey: {'*' * 16 if config.get('authkey') else '(not set)'}")
    print()
    
    # Create server
    handler_class = create_handler(config)
    server = ReusableHTTPServer((host, port), handler_class)
    
    # Setup SSL/TLS
    # Automatically use TLS for port 443, HTTP for port 80
    # Can be overridden with ATOP_MOCK_USE_SSL environment variable
    use_ssl = False
    if os.getenv('ATOP_MOCK_USE_SSL', '').lower() in ('1', 'true', 'yes'):
        use_ssl = True
    elif os.getenv('ATOP_MOCK_USE_SSL', '').lower() in ('0', 'false', 'no'):
        use_ssl = False
    else:
        # Auto-detect based on port
        use_ssl = (port == 443)
    
    if use_ssl:
        config_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'config')
        cert_file = os.getenv('ATOP_MOCK_CERT', os.path.join(config_dir, 'cert.pem'))
        key_file = os.getenv('ATOP_MOCK_KEY', os.path.join(config_dir, 'key.pem'))
        
        if os.path.exists(cert_file) and os.path.exists(key_file):
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.set_ciphers('ECDHE+AESGCM')
            context.load_cert_chain(cert_file, key_file)
            server.socket = context.wrap_socket(server.socket, server_side=True)
            print(f"🔒 SSL/TLS enabled (cert: {cert_file}, key: {key_file})")
        else:
            print(f"⚠️  SSL requested but cert/key files not found, using plain HTTP")
    
    protocol = 'https' if use_ssl else 'http'
    print(f"🚀 ATOP Mock Server started on {protocol}://{host}:{port}")
    print(f"   Endpoint: /d.json")
    print(f"   Protocol: {'HTTPS (TLS)' if use_ssl else 'HTTP'}")
    print(f"   Supported APIs:")
    print(f"     - thing.device.opensdk.active (device activation)")
    print(f"     - thing.ai.agent.token.get (AI token)")
    print(f"     - tuya.device.qrcode.info.get (QR code info)")
    print(f"     - tuya.device.meta.save (device meta save)")
    print()
    if port == 80 or port == 443:
        print(f"⚠️  Note: Binding to port {port} may require root privileges")
        print(f"   If you get 'Permission denied', try:")
        print(f"     - Use sudo: sudo python3 atop_mock.py")
        print(f"     - Or use a different port (e.g., 8080 for HTTP, 8443 for HTTPS)")
    print()
    print("Press Ctrl+C to stop the server")
    print()
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n🛑 Shutting down server...")
        server.shutdown()
        print("✅ Server stopped")


if __name__ == '__main__':
    main()

