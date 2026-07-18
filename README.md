# SF900 ESP32 Dongle — wireless gamepad receiver + Wi-Fi dongle for the R36S (and friends)

Turn an **ESP32-S3** into a **dual-mode USB dongle** for handheld retro consoles
like the R36S / R36H and clones running ArkOS / dArkOS / ROCKNIX:

- 🎮 **Wireless gamepad receiver** — receives the **Data Frog SF900 / SF2000**
  2.4 GHz controller and presents it to the console as a standard **USB-HID gamepad**.
- 📶 **USB Wi-Fi dongle** — presents as a **USB RNDIS network adapter** (works on the
  R36S Linux *and* on Windows 10) so a Wi-Fi-less handheld gets online for scraping,
  RetroAchievements, netplay and ROM transfer.
- 🔁 **Both in one board**, switched on the fly by holding **L + R + SELECT** on the
  controller (~1.5 s). The last mode used is remembered across reboots.

> This started as "how do I get Wi-Fi and a wireless controller on my R36S clone
> without buying anything" and ended as a from-scratch RF receiver. The radio was
> **transplanted from a dead SF2000 board** (the `XN297LBW` chip + crystal + antenna),
> but you can also use any cheap `XN297L` module — see [docs/WIRING.md](docs/WIRING.md).
>
> 📖 **The full story — cutting the dead SF2000 board, the transplant, the RF bring-up,
> the dead ends, and the breakthrough — is in [docs/BUILD-LOG.md](docs/BUILD-LOG.md).**

## Why this exists

The SF900 controller and the SF2000-style consoles talk over a proprietary 2.4 GHz
link using the **Panchip XN297L** transceiver (an nRF24-like chip). There was no open
receiver you could plug into a *different* console. Meanwhile the R36S has no built-in
Wi-Fi. This project solves both with a single, cheap ESP32-S3.

The RF protocol itself was **reverse-engineered by [axgdev/UniFrog](https://github.com/axgdev/UniFrog)** —
this repo reimplements it on the ESP32 and adds the USB-HID + Wi-Fi dongle sides.

## Repository layout

```
firmware/
  esp32s3-gamepad/     SF900 receiver -> USB-HID gamepad  (boots in OTA slot 0)
  esp32s3-wifi-dongle/ USB RNDIS Wi-Fi dongle + mode-switch watcher (OTA slot 1)
  xn297-selftest/      bring-up tools: SPI selftest, RSSI band scanner, SF900 receiver-to-serial
  xn297-sniffer/       passive SPI-slave sniffer (for reverse-engineering other controllers)
docs/
  BUILD-LOG.md         the full story: dead SF2000 -> transplant -> receiver -> dual-mode
  PROTOCOL.md          the SF900 / SF2000 RF protocol, fully documented
  XN297-TRANSPLANT.md  step-by-step: cutting the radio out of a dead SF2000 and validating it
  WIRING.md            pinout / connections (ESP32-S3 <-> XN297L)
scripts/
  flash-dual-mode.ps1  flash both firmwares into the two OTA slots (Windows)
  flash-dual-mode.sh   same, for Linux/macOS
```

## Hardware

- **ESP32-S3** dev board (uses the native USB port for the gamepad/network interface).
- An **XN297L / XN297LBW** 2.4 GHz module + 16 MHz crystal + antenna. Either a ready
  module, or transplanted from a dead SF2000 board (see the transplant guide).
- A **USB-C OTG adapter** to plug the board's native-USB into the console's bottom port.

Wiring (3-wire SPI, see [docs/WIRING.md](docs/WIRING.md)):

| XN297L | ESP32-S3 | Function        |
|--------|----------|-----------------|
| CSN    | GPIO10   | FSPICS0         |
| SCK    | GPIO12   | FSPICLK         |
| DATA   | GPIO11   | FSPID (3-wire)  |
| VDD    | 3V3      | 2.3–3.3 V only  |
| VSS    | GND      | ground          |

## Build & flash

Requires [PlatformIO](https://platformio.org/) (the ESP-IDF platform is pulled
automatically on first build).

```bash
# gamepad firmware
cd firmware/esp32s3-gamepad && pio run

# wifi dongle firmware
cd firmware/esp32s3-wifi-dongle && pio run
```

To get the **dual-mode** setup (both firmwares in the two OTA slots, gamepad boots
first), build both then run the flash script:

```powershell
# Windows (PowerShell) — adjust the COM port inside the script
scripts\flash-dual-mode.ps1 -Port COM19
```

```bash
# Linux / macOS
scripts/flash-dual-mode.sh /dev/ttyACM0
```

The script writes: bootloader, partition table, `ota_0`=gamepad, `ota_1`=wifi, and
erases `otadata` so the board boots the gamepad first.

> **Flashing note:** flash over the board's **UART/COM** port. The **native USB** port
> is what becomes the gamepad / network device at runtime.

## Using it

1. Plug the board's **native USB** into the console via a USB-C OTG adapter.
2. **Gamepad mode (default):** turn on the SF900 controller and play. The console sees
   a standard HID gamepad (D-pad on the hat, A/B/X/Y/L/R/SELECT/START on buttons 0–7).
3. **Switch to Wi-Fi mode:** hold **L + R + SELECT** on the controller for ~1.5 s. The
   dongle reboots as a USB network adapter. Configure Wi-Fi once over its USB-serial
   console: `sta -s <SSID> -p <password>` (2.4 GHz only). Credentials are saved.
4. **Switch back:** hold **L + R + SELECT** again. The board remembers the last mode.

## How the mode switch works

USB device descriptors are fixed at compile time, so one binary can't be *both* a
network device and a HID gamepad. Instead the two firmwares live in **two OTA app
slots**; holding the combo calls `esp_ota_set_boot_partition()` and reboots into the
other slot. Each firmware watches the controller for the combo (the Wi-Fi firmware
runs a tiny background XN297 listener that doesn't disturb Wi-Fi).

> The design originally used the **BOOT button** to switch, but that button proved
> unreliable on the test board, so the controller combo became the trigger — which is
> nicer anyway since the controller is already in your hand.

## Status

Everything here is **working and validated on real hardware**: SPI selftest passes,
the RSSI scanner sees the controller on channels 4/29/49/79, buttons decode correctly,
the USB-HID gamepad is recognized by Windows and the console, the RNDIS Wi-Fi dongle
gets online, and both mode-switch directions work.

## Credits

- **[axgdev/UniFrog](https://github.com/axgdev/UniFrog)** — reverse-engineered the SF900/SF2000 RF protocol. This project would not exist without it.
- **[Espressif esp-iot-solution](https://github.com/espressif/esp-iot-solution)** — the `usb_dongle` example the Wi-Fi side is built on.
- **Panchip** — XN297L datasheet.
- The R36S / ArkOS / dArkOS4Clone / ROCKNIX communities.

## License

MIT — see [LICENSE](LICENSE). Bundled third-party components retain their own licenses
(Apache-2.0 / MIT); attribution is in the LICENSE file.
