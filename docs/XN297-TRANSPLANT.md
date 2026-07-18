# Transplanting the XN297LBW radio from a dead SF2000 board

You don't need to buy an XN297L module — you can harvest the whole radio section from a
dead SF2000 mainboard (or a dead SF900 controller). This is optional; a cheap XN297L
module works just as well and needs no cutting.

> Safety first: the chip is CMOS — use an ESD strap. Do all continuity testing with the
> battery **removed** (dead board). Protect the flex antenna's solder joint with Kapton
> and never let it flex.

## What survives the cut

Keep, as one piece: the **XN297LBW**, the **16 MHz crystal + its load caps**, the RF
**matching network** (`[Z]`, the L/C next to pin 8), the **flex antenna + its solder
joint**, and a **100 nF** decoupling cap near VDD if present. Everything else (the dead
SoC, old SPI traces, old power trace) gets cut away.

## Pinout (XN297LBW, SOP-8, top view)

```
            ┌───────U───────┐
   CSN  ─ 1 ┤●              ├ 8 ─ ANT
   SCK  ─ 2 ┤   XN297LBW    ├ 7 ─ VSS
   DATA ─ 3 ┤   (SOP-8)     ├ 6 ─ XC2
   VDD  ─ 4 ┤               ├ 5 ─ XC1
            └───────────────┘
```

Don't trust the silkscreen dot for pin 1 — confirm orientation with a multimeter:
pin 7 ↔ ground plane, pins 5/6 ↔ crystal terminals, pin 8 ↔ first matching component
toward the antenna. Then CSN/SCK/DATA are pins 1/2/3 by elimination.

## Procedure (short form)

1. **Map & mark (before cutting).** With the board dead, find the 5 solder points you'll
   use — CSN, SCK, DATA (chip pins 1/2/3, follow their traces to a nearby via), plus a
   VDD point and a GND point inside the region you'll keep. Mark them.
2. **Cut** the radio section free with ≥ 5 mm clearance from any kept component. Lightly
   sand the cut edge; confirm VDD↔VSS is open (not shorted by smeared copper).
3. **Re-check** each marked point still beeps to its chip pin (the cut may have severed a
   trace — if so, find an alternative point closer to the chip).
4. **Solder** 5 thin (30 AWG) wires to the marked points. Add strain relief (hot glue).
5. Wire to the ESP32-S3 per [WIRING.md](WIRING.md) and run the selftest below.

## Bring-up / validation

The `firmware/xn297-selftest` project contains the tools used to validate the transplant,
in order:

1. **SPI selftest** — reads `STATUS` (expect `0x0E`) and echoes the `TX_ADDR` register
   (`DE AD BE EF 42`). Proves wiring + SPI are good, independent of RF.
2. **RSSI band scanner** — puts the chip in RX with full calibration and reads the
   realtime-RSSI nibble (reg 0x09) per channel. Hold a button on the SF900 nearby and you
   should see energy light up on channels 4/29/49/79 (2404–2479 MHz) — proof the whole RF
   chain (antenna → matching → LNA → demod) works.
3. **SF900 receiver** — the full protocol receiver, printing decoded buttons to serial.

(The `firmware/xn297-sniffer` project is a passive SPI-slave sniffer, useful if you want
to reverse-engineer a *different* controller by tapping its XN297's SPI bus.)

## Common failures

| Symptom (selftest)          | Likely cause                                  |
|-----------------------------|-----------------------------------------------|
| STATUS/echo all `0x00`      | DATA or SCK not connected (wire/solder/pin)   |
| STATUS/echo all `0xFF`      | DATA stuck high — check CSN and VDD-edge short |
| STATUS `0x0E`, echo garbled | SPI clock too high or noise — shorten wires, add 100 nF |
| SPI OK but RSSI scan flat   | missing calibration writes — use the full init (see PROTOCOL.md) |
