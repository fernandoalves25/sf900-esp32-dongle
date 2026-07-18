# Build log — how this project came together

The story, start to finish, in case it helps someone attempting the same thing (or a
similar transplant on another console). It did not go in a straight line.

## 0. The starting point

An **R36S clone** (K36-type panel) that needed two things it didn't have: **Wi-Fi** and a
**wireless controller**. The wireless controller in question was a **Data Frog SF900**,
which pairs with SF2000-style consoles over a proprietary 2.4 GHz link. There was also a
**dead SF2000 mainboard** in a drawer.

Two goals fell out of that:
1. Give the R36S USB Wi-Fi (it has none built in).
2. Make the SF900 controller work on the R36S, which doesn't natively speak its protocol.

## 1. Wi-Fi first (the easy half)

An ESP32-S3 can present itself to the console as a **USB network adapter**. Built on
Espressif's `usb_dongle` example, first in **ECM** mode — which the R36S's Linux kernel
*didn't* pick up (stuck at 127.0.0.1). Reflashed in **RNDIS** mode (the same class phone
USB-tethering uses) and it worked: the dongle bridges the ESP32's Wi-Fi to a USB network
interface. A traffic-reactive RGB LED and NVS-saved credentials were added. This is the
`esp32s3-wifi-dongle` firmware.

(A cheap USB Wi-Fi dongle the user already owned turned out to use a **ZTOP ZT9101** chip
with no ArkOS driver — hence building the ESP32 one instead.)

## 2. The transplant (the hard half begins)

The SF900 and the SF2000 both use the **Panchip XN297L** transceiver. The plan: cut the
radio section (chip + 16 MHz crystal + matching network + flex antenna) out of the dead
SF2000 board and drive it from the ESP32 over 3-wire SPI. Full procedure in
[XN297-TRANSPLANT.md](XN297-TRANSPLANT.md): map the traces with a multimeter *before*
cutting, cut with 5 mm clearance, solder 5 thin wires to the marked vias, wire to the
ESP32's native FSPI pins (GPIO10/11/12).

**First win:** the SPI **selftest** passed — `STATUS = 0x0E`, and the `TX_ADDR` register
echoed back `DE AD BE EF 42`. The transplanted chip was alive and talking.

## 3. "Alive" ≠ "receiving"

A working SPI selftest only proves the digital side. The first attempt at a **carrier
scan** saw *nothing* across the whole 2.4 GHz band while holding controller buttons. Two
mistakes, both instructive:

- The RX front-end needs its **calibration registers** written at init (`BB_CAL`,
  `RF_CAL`, `DEM_CAL`). A "bare" init reads/writes registers fine but the radio is deaf.
- Register `0x09` on the XN297L isn't an nRF24-style 1-bit carrier-detect — it's an
  **RSSI** value (high nibble = realtime RSSI). Reading it as a bit always returned 0.

With the full calibration init and RSSI enabled (`RF_CAL` bit 15), the **RSSI band
scanner** lit up: baseline (controller off) vs. button-held showed strong energy appear on
specific channels, including the otherwise-dead top of the band. **The transplant
receives.** The strongest channels — 2404 / 2429 / 2449 / 2479 MHz — would turn out to be
exactly right.

## 4. Receiving energy ≠ decoding packets

The SF900 **frequency-hops**, and the XN297/nRF24 packet engine only fills its FIFO on an
**exact address match** — there's no true promiscuous mode. A promiscuous sniff (no CRC,
wildcard address, all data rates) synced with nothing, as expected against a hopping
transmitter with its own address. To decode buttons we needed the controller's exact
parameters: address, hop sequence, data rate, payload format.

## 5. The breakthrough: axgdev/UniFrog

Rather than crack open the controller and capture its SPI, the parameters turned up
already reverse-engineered in **[axgdev/UniFrog](https://github.com/axgdev/UniFrog)**
(`unifrog_input_wireless.c`). Everything was there: 5-byte addresses, hop channels
`{0x04, 0x1D, 0x31, 0x4F}` = **2404/2429/2449/2479 MHz — the exact channels the RSSI scan
had flagged**, 2-byte payload, calibration blobs, and the button bitmap. (The
`Data-Frog-Central/HC-RTOS` console repo, checked first, only had the *wired* button
driver — the wireless RX is a closed block there.)

Porting that config into the receiver (`xn297-selftest` → SF900 receiver) worked
immediately: **274 packets, buttons decoding correctly** — R, Y, UP, DOWN, LEFT, RIGHT, B.
All credit for the protocol to UniFrog.

## 6. Making it a real gamepad

A receiver that prints buttons to serial isn't a controller. The `esp32s3-gamepad`
firmware wraps the receiver in **USB-HID**: it enumerates as a standard gamepad (D-pad on
the HID hat, A/B/X/Y/L/R/SELECT/START on buttons 0–7). Windows and the console recognize it
with no driver. **You can now play the R36S wirelessly with a controller built from a dead
console's radio.**

## 7. Both modes in one board

The last ask was to combine Wi-Fi and gamepad into one dongle, switchable on the fly. Two
snags shaped the final design:

- **USB descriptors are compile-time**, so one binary can't be *both* a network device and
  a HID gamepad. Solution: put the two firmwares in **two OTA app slots** and switch which
  one boots.
- The intended trigger, the **BOOT button**, turned out **dead** on this board (reads a
  constant high at runtime — the same flaky button that kept dropping the board into
  download mode all along). Solution, arguably better: switch by **holding L + R + SELECT
  on the controller** (~1.5 s). The gamepad firmware watches for it directly; the Wi-Fi
  firmware runs a tiny background XN297 listener that watches for the same combo without
  disturbing Wi-Fi. `esp_ota_set_boot_partition()` + reboot does the switch, and the last
  mode is remembered.

A subtle bug cost some time here: **PlatformIO ignores `CONFIG_PARTITION_TABLE_CUSTOM` in
sdkconfig** and builds a default single-`factory` table unless you set
`board_build.partitions` in `platformio.ini`. With the wrong table there were no OTA slots,
so `esp_ota_set_boot_partition()` returned `INVALID_ARG` and the switch silently did
nothing. Once fixed, both switch directions worked.

## Result

One cheap ESP32-S3 + a radio harvested from a dead SF2000 = a dual-mode dongle that gives a
Wi-Fi-less retro handheld both **online access** and a **wireless controller**, switchable
from the controller itself. Every stage is validated on real hardware.

## Tools left in the repo for the next person

- `xn297-selftest` — SPI selftest, RSSI band scanner, and the SF900 receiver-to-serial.
  The exact sequence to bring up and validate a transplant.
- `xn297-sniffer` — a passive SPI-slave sniffer, for reverse-engineering a *different*
  controller by tapping its XN297's SPI bus (the route we didn't need thanks to UniFrog,
  but which works for chips nobody has documented yet).
