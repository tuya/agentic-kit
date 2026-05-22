#!/usr/bin/env bash
#
# activate-demo.sh
#
# End-to-end API-based device activation demo.
# Assumes this script, tuya_openapi.py, and activate_demo binary are all
# in the same directory (produced by CMake build).
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# -- Configuration (edit these or export before running) -----------------------

export TUYA_CLIENT_ID="${TUYA_CLIENT_ID:-your_client_id}"
export TUYA_CLIENT_SECRET="${TUYA_CLIENT_SECRET:-your_secret}"
export TUYA_BASE_URL="${TUYA_BASE_URL:-https://openapi.tuyacn.com}"

SCHEMA="${SCHEMA:-myapp}"
COUNTRY_CODE="${COUNTRY_CODE:-86}"
USERNAME="${USERNAME:-17a4b8b5ca1a43c8bd56e2678fba6740}"
PASSWORD="${PASSWORD:-mypassword}"
USERNAME_TYPE="${USERNAME_TYPE:-3}"
TIME_ZONE_ID="${TIME_ZONE_ID:-Asia/Shanghai}"

PARING_TYPE="${PARING_TYPE:-BLE}"
DEVICE_UUID="${DEVICE_UUID:-5682bceac872cfe7}"

# Device credentials (passed to the C activate_demo binary)
UUID="${UUID:-uuid17a65d2314ac60f5}"
AUTHKEY="${AUTHKEY:-tNn74X0lff222ocdUVVFYmjP15oWr9Vn}"
PRODUCT_KEY="${PRODUCT_KEY:-5gkeobhit9sd6odu}"
FIRMWARE_KEY="${FIRMWARE_KEY:-}"

# -- Step 1: Create / sync a user ---------------------------------------------

echo "=== Step 1: Sync user ==="
python3 ./tuya_openapi.py sync-user \
    --schema "$SCHEMA" --country-code "$COUNTRY_CODE" \
    --username "$USERNAME" --password "$PASSWORD" \
    --username-type "$USERNAME_TYPE" --time-zone-id "$TIME_ZONE_ID"

# Extract uid from sync-user response
SYNC_RESP=$(python3 ./tuya_openapi.py sync-user \
    --schema "$SCHEMA" --country-code "$COUNTRY_CODE" \
    --username "$USERNAME" --password "$PASSWORD" \
    --username-type "$USERNAME_TYPE" --time-zone-id "$TIME_ZONE_ID")
USER_UID=$(echo "$SYNC_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['uid'])")
echo "User UID: $USER_UID"

# -- Step 2: Generate pairing token -------------------------------------------

echo ""
echo "=== Step 2: Generate pairing token ==="
TOKEN_RESP=$(python3 ./tuya_openapi.py pairing-token \
    --uid "$USER_UID" --paring-type "$PARING_TYPE" \
    --time-zone-id "$TIME_ZONE_ID" --uuid "$DEVICE_UUID")
echo "$TOKEN_RESP"

TOKEN=$(echo "$TOKEN_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['token'])")
REGION=$(echo "$TOKEN_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['region'])")
SECRET=$(echo "$TOKEN_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['secret'])")
PAIRING_TOKEN="${REGION}${TOKEN}${SECRET}"
echo "Pairing token: $PAIRING_TOKEN"

# -- Step 3: Activate device with the token ------------------------------------

echo ""
echo "=== Step 3: Activate device ==="
./activate_demo "$PAIRING_TOKEN" "$UUID" "$AUTHKEY" "$PRODUCT_KEY" "$FIRMWARE_KEY"

# -- Step 4: Poll for pairing result -------------------------------------------

echo ""
echo "=== Step 4: Poll pairing result ==="
python3 ./tuya_openapi.py pairing-result --token "$TOKEN" --poll
