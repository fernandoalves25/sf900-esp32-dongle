#!/usr/bin/env bash
# Flash both firmwares into the two OTA slots (Linux / macOS).
#
#   scripts/flash-dual-mode.sh /dev/ttyACM0 [baud]
#
# Build both projects first:
#   (cd firmware/esp32s3-gamepad     && pio run)
#   (cd firmware/esp32s3-wifi-dongle && pio run)
#
# Layout: bootloader@0x0, partition-table@0x8000, ota_0=gamepad@0x10000,
#         ota_1=wifi@0x1e0000. otadata is erased so the board boots the gamepad first.
set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
BAUD="${2:-460800}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
G="$ROOT/firmware/esp32s3-gamepad/.pio/build/esp32s3"
W="$ROOT/firmware/esp32s3-wifi-dongle/.pio/build/esp32s3"

for f in "$G/bootloader.bin" "$G/partitions.bin" "$G/firmware.bin" "$W/firmware.bin"; do
    [ -f "$f" ] || { echo "Missing $f — run 'pio run' in both firmware projects first."; exit 1; }
done

echo "Flashing dual-mode image to $PORT ..."
python -m esptool --chip esp32s3 --port "$PORT" --baud "$BAUD" write-flash \
    0x0      "$G/bootloader.bin" \
    0x8000   "$G/partitions.bin" \
    0x10000  "$G/firmware.bin" \
    0x1e0000 "$W/firmware.bin"

echo "Erasing otadata (boot gamepad first) ..."
python -m esptool --chip esp32s3 --port "$PORT" --after hard-reset erase-region 0xd000 0x2000

echo "Done. Boots in gamepad mode. Hold L+R+SELECT on the controller to switch modes."
