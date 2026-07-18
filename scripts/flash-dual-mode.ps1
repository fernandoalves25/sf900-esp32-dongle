# Flash both firmwares into the two OTA slots (Windows / PowerShell).
#
#   scripts\flash-dual-mode.ps1 -Port COM19
#
# Build both projects first:
#   cd firmware\esp32s3-gamepad     ; pio run
#   cd firmware\esp32s3-wifi-dongle ; pio run
#
# Layout: bootloader@0x0, partition-table@0x8000, ota_0=gamepad@0x10000,
#         ota_1=wifi@0x1e0000. otadata is erased so the board boots the gamepad first.

param(
    [string]$Port = "COM19",
    [int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$g = Join-Path $root "firmware\esp32s3-gamepad\.pio\build\esp32s3"
$w = Join-Path $root "firmware\esp32s3-wifi-dongle\.pio\build\esp32s3"

foreach ($f in @("$g\bootloader.bin","$g\partitions.bin","$g\firmware.bin","$w\firmware.bin")) {
    if (-not (Test-Path $f)) { throw "Missing $f — run 'pio run' in both firmware projects first." }
}

Write-Host "Flashing dual-mode image to $Port ..."
python -m esptool --chip esp32s3 --port $Port --baud $Baud write-flash `
    0x0      "$g\bootloader.bin" `
    0x8000   "$g\partitions.bin" `
    0x10000  "$g\firmware.bin" `
    0x1e0000 "$w\firmware.bin"

Write-Host "Erasing otadata (boot gamepad first) ..."
python -m esptool --chip esp32s3 --port $Port --after hard-reset erase-region 0xd000 0x2000

Write-Host "Done. Boots in gamepad mode. Hold L+R+SELECT on the controller to switch modes."
