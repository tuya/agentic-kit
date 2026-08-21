# APP-confirmed OTA demo (macOS/POSIX)

This demo uses an already-activated device, so it does not run pairing or
activation. It connects the device to MQTT with `devid`, `secret_key`, and
`local_key`, waits for the app user to confirm the cloud OTA task, and then
performs `tuya.device.upgrade.get` from the application thread.

## Build on macOS

```bash
cmake -S examples/posix -B build/examples -DCMAKE_BUILD_TYPE=Debug
cmake --build build/examples --target ota_confirm_demo
```

If your Mac's Perl reports an unsupported `C.UTF-8` locale during the mbedTLS
generated-source step, build with:

```bash
LC_ALL=C LANG=C cmake --build build/examples --target ota_confirm_demo
```

## Run

```bash
export OTA_DEVID='your-device-id'
export OTA_SECRET_KEY='your-device-secret-key'
export OTA_LOCAL_KEY='your-device-local-key'
export OTA_REGION='AY'   # AY, AZ, UEAZ, EU, WEAZ, IN, or SG

./build/examples/ota_confirm_demo
```

The demo prints `APP-confirmed OTA notice received (channel=N)` when the cloud
pushes MQTT protocol 15. It then queries that channel and prints the returned
firmware metadata.

To download the confirmed image, verify its cloud digest, and report
UPGRADING/COMPLETE/ERROR:

```bash
./build/examples/ota_confirm_demo --download
```

Credentials can also be passed positionally:

```bash
./build/examples/ota_confirm_demo "$OTA_DEVID" "$OTA_SECRET_KEY" "$OTA_LOCAL_KEY" "$OTA_REGION"
```

Keep the cloud OTA task configured as APP-confirm mode. The demo never calls
the silent-upgrade API, so the device should query only after the app confirms.
