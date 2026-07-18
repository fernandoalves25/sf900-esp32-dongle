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

## Using a transplanted SF2000 radio

If you cut the radio section out of a dead SF2000 board (chip + crystal + matching
network + flex antenna), the same 5 wires apply — solder to the XN297LBW's CSN/SCK/DATA
pins (or their nearest vias) and a VDD/GND point. See
[XN297-TRANSPLANT.md](XN297-TRANSPLANT.md).
