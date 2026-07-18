# Wiring — ESP32-S3 ↔ XN297L

The XN297L uses a **3-wire SPI** bus (the `DATA` line is bidirectional). Only 5 wires go
to the ESP32-S3; the crystal and antenna live on the module itself.

## Connections

| Signal | XN297L pin | ESP32-S3 | IO MUX function      |
|--------|-----------|----------|----------------------|
| CSN    | 1         | GPIO10   | FSPICS0 (chip select) |
| SCK    | 2         | GPIO12   | FSPICLK              |
| DATA   | 3         | GPIO11   | FSPID (bidirectional) |
| VDD    | 4         | 3V3      | 2.3–3.3 V (never 5 V) |
| VSS    | 7         | GND      | ground               |

Pins 5, 6 (crystal XC1/XC2) and 8 (antenna) stay on the module — do **not** connect them
to the ESP32.

```
  ESP32-S3                     XN297L module
  ┌───────────────┐            ┌────────────────────────┐
  │ 3V3   ───────────── red ───┤ VDD (4)                │
  │ GND   ───────────── blk ───┤ VSS (7)                │
  │ GPIO10 (FSPICS0) ── yel ───┤ CSN (1)                │
  │ GPIO12 (FSPICLK) ── grn ───┤ SCK (2)                │
  │ GPIO11 (FSPID)  ─── blu ──►┤ DATA (3)  (bidir)      │
  │                  │         │  ┌──────┐   ╔════════╗ │
  │ native USB ──────┼──► host │  │16 MHz│   ║ antenna║ │
  └───────────────┘            │  └──────┘   ╚════════╝ │
                               └────────────────────────┘
```

## Why these GPIOs

GPIO10/11/12 are the ESP32-S3's **native FSPI (SPI2) IO-MUX pins**, giving the cleanest
signal. Avoid: 19/20 (USB), 26–32 (flash), 33–37 (octal PSRAM on N8R8 modules),
0/3/45/46 (strapping), 48 (RGB LED on many dev boards), and whatever your board uses for
its UART.

## Electrical notes

- **3.3 V only.** The XN297L absolute max is 3.6 V. No level shifting is needed — the
  ESP32-S3 is a 3.3 V part.
- Keep the 5 wires short (≤ 10 cm). A 100 nF decoupling cap right at the module's VDD/GND
  helps RF reception when powered over flying leads.
- **No pull-ups/pull-downs** are required on the SPI lines.
- Keep the module's antenna as far as possible from the ESP32's own Wi-Fi antenna
  (opposite ends of the board) to reduce interference.

## Which radio module to use

You have two ways to get the radio:

### Option A — transplant it from a dead SF2000 (what this project used)

The exact part harvested here is the **Panchip XN297LBW** (SOP-8), together with its
**16 MHz crystal (marked "16.000")**, the factory **matching network** and the **flex
antenna** — all cut out of a dead SF2000 mainboard as one piece. It comes pre-matched to a
real antenna, so it has the best range. The controller (SF900) uses the sibling
**XN297LBN**. Full procedure in [XN297-TRANSPLANT.md](XN297-TRANSPLANT.md); wire the
XN297LBW's CSN/SCK/DATA pins (or their nearest vias) plus a VDD/GND point per the table
above.

### Option B — buy a ready module (no cutting needed)

Any **Panchip XN297L 2.4 GHz module** is a drop-in replacement — same firmware, same
5 wires, no code changes. Search for:

- **"XN297L module 2.4G"**
- **"XN297LBW module"**
- **"XL2400" / "XL2400P"** — newer Panchip parts in the same family, usually pin/command compatible

These come with the chip + 16 MHz crystal + antenna already on the board; you just wire the
5 lines. A module with a **u.FL connector + external antenna** gives the best range; a plain
PCB-antenna module is fine for a controller used within a few meters.

> ### ⚠️ It MUST be an XN297L — an nRF24L01 will NOT work
>
> The XN297 shares the nRF24L01's **SPI command set** (so it looks like a clone), but it
> adds an over-the-air **data whitening / scramble** layer that the nRF24 doesn't have.
> This firmware relies on the XN297L doing that de-scrambling **in hardware** (the
> `SCRAMBLE_EN` bit in `DEM_CAL`). A plain nRF24L01 / nRF24L01+ cannot receive the SF900's
> packets without significant extra software.
>
> - ✅ **Works:** XN297L, XN297LBW, XN297LBN, XL2400 / XL2400P
> - ❌ **Does not work:** nRF24L01, nRF24L01+, Si24R1, and other non-XN297 chips
>
> Also make sure the module runs at **3.3 V** (the XN297L abs-max is 3.6 V — never 5 V).
