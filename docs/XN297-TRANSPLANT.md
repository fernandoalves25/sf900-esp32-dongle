# Transplanting the XN297LBW radio from a dead SF2000 board

This is the full, step-by-step process used in this project: cutting the 2.4 GHz radio
section out of a **dead SF2000 mainboard** and wiring it to an ESP32-S3. It's optional —
a cheap `XN297L` module works just as well and needs no cutting (skip to
[WIRING.md](WIRING.md)) — but if you have a bricked SF2000 lying around, its radio is
already matched to a real antenna and free.

Everything here was cross-checked against the **Panchip XN297L datasheet v5.2**.

> **The idea:** the SF2000's radio section (the `XN297LBW` chip + 16 MHz crystal +
> matching network + flex antenna) is a self-contained RF front-end. If the console's
> SoC is dead but the radio corner is intact, cut it free, bring out the 3-wire SPI +
> power, and drive it from any host. That turns a paperweight into a working transceiver.

---

## ⚠️ Rules to read before touching the board

1. **ESD:** the chip is CMOS. Wear an antistatic strap, or at minimum touch a grounded
   point before handling. With a dead board this is the only real risk of the measuring.
2. **Continuity only on a dead board:** remove the 18650 battery before any measurement.
3. **Flex antenna:** its solder joint must not bend or vibrate. Cover it with Kapton for
   the whole job.
4. **SPI at ≤ 1 MHz during the entire init.** The XN297L only accepts `W_REGISTER` in
   power-down/standby, where SPI is limited to 1 Mbps. At 4 MHz config writes corrupt
   **silently** (including the calibration registers).
5. **3.3 V, never 5 V.** Chip range 2.3–3.3 V (abs max 3.6 V).
6. **Matching network `[Z]`:** do not change the factory L/C values.
7. **No pressure connections on SPI** (clips, hooks, leaning wires) — intermittent
   contact produces exactly the ghosts in the troubleshooting table. On the module it's
   solder.
8. Pins **5, 6, 8 do not go to the ESP32** — crystal and antenna stay on the module.

---

## Stage 0 — Pinout reference (XN297LBW, SOP-8, top view)

```
            ┌───────U───────┐
   CSN  ─ 1 ┤●              ├ 8 ─ ANT
   SCK  ─ 2 ┤   XN297LBW    ├ 7 ─ VSS (GND)
   DATA ─ 3 ┤   (SOP-8)     ├ 6 ─ XC2
   VDD  ─ 4 ┤               ├ 5 ─ XC1
            └───────────────┘
```

| Pin | Name | Function |
|-----|------|----------|
| 1 | CSN  | SPI chip select (active low) |
| 2 | SCK  | SPI clock |
| 3 | DATA | SPI data, **bidirectional** (3-wire) |
| 4 | VDD  | +2.3 to +3.3 V |
| 5 | XC1  | crystal in |
| 6 | XC2  | crystal out |
| 7 | VSS  | ground |
| 8 | ANT  | antenna (RF) |

There is no CE or IRQ pin on this SOP-8 package: CE is controlled by SPI command
(`FEATURE` reg `0x1D` bit 5 = 1, then `0xFD 00` = CE on, `0xFC 00` = CE off), and status
is read by polling `STATUS` (reg `0x07`).

> **Don't trust the silkscreen dot for pin 1** — the dot's position varies between lots.
> Confirm orientation with the multimeter in Stage 1B.

---

## Stage 1 — Map the board and hunt the traces (before cutting)

Goal: leave this stage with the chip orientation confirmed and **5 solder points marked**
on the board. With the board still whole the multimeter has more to work with, so the cut
and the soldering afterwards become paint-by-numbers. ~20–30 min of beeping.

### 1A · Prep & visual map

- [ ] Remove the 18650 (continuity is only valid on a fully dead board)
- [ ] Touch a grounded point (ESD)
- [ ] Find the **XN297LBW** (SOP-8, marked "XN297LBW" / "XN297LBN")
- [ ] Find the **16 MHz crystal** (marked "16.000") and its two load caps
- [ ] Find the **matching network** `[Z]` (L/C 0402) between pin 8 and the flex antenna
- [ ] Find the **flex antenna's solder joint** — from here on, no bending/vibrating
- [ ] Find the **100 nF** decoupling cap near pin 4 (if present)
- [ ] **Photograph** the whole area in high resolution

```
   ANTENNA FLEX          solder    ┌── SoC (DEAD) ──┐
  ╔═════════════╗        joint     │  CSN SCK DATA  │  ← traces to KILL
  ║ ┌─┐┌─┐┌─┐   ║──●───[Z]──┐      └───┬───┬───┬────┘     in the cut
  ╚═════════════╝  │        │  ┌───────┴───┴───┴──┐
                   │        └──┤ 8   1   2    3    │
                  ─┴─ GND       │    XN297LBW      │
                                │ 4   7   5    6   │
                          +3V3 ─┘    │   │    │
                       (100 nF)     GND ┌──┴─┐
                                        │16MHz│ + C1/C2
                                        └─────┘
```

### 1B · Confirm chip orientation (continuity)

- [ ] Pin **7** ↔ ground plane (several points): continuity
- [ ] Pin **6** ↔ one crystal terminal: continuity
- [ ] Pin **5** ↔ the other crystal terminal: continuity
- [ ] Pin **4** ↔ nearby decoupling cap: continuity — and **not** shorted to GND
- [ ] Pin **8** ↔ first matching component toward the antenna: continuity

> If 5/6/7/8 check out, then **1 = CSN, 2 = SCK, 3 = DATA** by elimination.

### 1C · Hunt the solder points

Only 4 real hunts: pins 1, 2, 3, 4. Pin 7 beeps anywhere on the ground plane; pins 5/6/8
are internal to the kept module.

**Technique:** one probe held still on the chip pin (tape/putty it down), the other
sweeping vias/pads inside the future kept zone until it beeps → that's the solder point
for that signal: **mark with a fine permanent pen + photograph.** Always pin → via, never
pin → pin (the chip's protection diodes confuse pin-to-pin readings).

- [ ] Pin **1 (CSN)** → point marked
- [ ] Pin **2 (SCK)** → point marked
- [ ] Pin **3 (DATA)** → point marked
- [ ] Pin **4 (VDD)** → point marked inside the kept zone (the 100 nF pad is the natural pick)
- [ ] A **GND** point chosen and marked

> Reading the beep: solid ~0–2 Ω = a direct trace (what you want). A short beep that dies
> = a cap charging (normal, especially on VDD). Tens of ohms = a series resistor in the
> path; the solder point is the pad *after* it.

**Exit criteria:** orientation confirmed + 5 points marked and photographed.

---

## Stage 2 — Cut the radio zone

Goal: physically separate the radio module from the rest of the board, killing the old
SPI and power traces. A clean cut here = zero rework later.

| Survives (inside the cut) | Dies (outside) |
|---------------------------|----------------|
| Flex antenna + solder joint | dead SoC |
| Matching network `[Z]`    | old SPI traces |
| XN297LBW                   | old power trace |
| 16 MHz crystal + C1/C2     | |
| 100 nF near pin 4 (if any) | |
| **the 5 marked points**   | |

- [ ] Cover the flex antenna and its joint with **Kapton**
- [ ] Mark the cut line with **≥ 5 mm** clearance from any live component
- [ ] Verify the **5 marked points are INSIDE** the line (generous margin = more trace stub to work with)
- [ ] Cut with a mini-saw / rotary tool at **low speed**, with pauses (don't heat the board)
- [ ] **Sand** the cut edges (removes burrs and smeared copper)
- [ ] Clean the dust with isopropyl alcohol + soft brush
- [ ] **Post-cut check:** pin 4 (VDD) × pin 7 (VSS) → must read **open** / high resistance

> If VDD × VSS shows a hard short, it's smeared copper on the cut edge. Sand the edge
> again, clean, inspect under magnification, re-measure. Don't proceed until it's open.

> **RF ground after the cut:** beyond the 5 mm, visually confirm the matching network's
> shunt elements and the antenna's ground return still have GND vias inside the kept area.
> A cut that eats the ground plane near the matching ruins the range even with perfect SPI.

**Exit criteria:** VDD × VSS open; edges sanded & inspected; antenna intact under Kapton;
the 5 points survived.

---

## Stage 3 — Re-check after the cut

Quick — the targets are already marked. Multimeter in continuity:

- [ ] Pin **1** ↔ CSN point: still beeps
- [ ] Pin **2** ↔ SCK point: still beeps
- [ ] Pin **3** ↔ DATA point: still beeps
- [ ] Pin **4** ↔ VDD point: still beeps
- [ ] Pin **7** ↔ GND point: still beeps
- [ ] (if you re-sanded) VDD × VSS → open

> If a point stopped beeping, the cut severed its trace. Hunt an alternative point closer
> to the chip. Last resort: solder that signal directly to the chip pin (possible, but
> double the thermal care — quick touch, flux, clean iron).

---

## Stage 4 — Solder the 5-wire harness

Goal: leave the module with 5 identified wires ready for the ESP32-S3. ~30 s per joint,
5 joints.

> **Why there's no solder-free shortcut:** alligator clips / hooks / leaning wires =
> intermittent contact, and intermittent contact on SPI produces exactly the ghosts in
> the troubleshooting table (sometimes all `0x00`, sometimes `0xFF`, corrupted echo) —
> and you won't know whether it's the transplant or the clip. The module needs mechanical
> fixing anyway.

- [ ] Scrape **2–3 mm of solder mask** over each marked point (a metallized via is even
      better — the hole anchors solder better than a thin trace)
- [ ] Flux + **tin** each point (iron ~320–350 °C, quick touches)
- [ ] Prepare 5 conductors, total length **≤ 10 cm** (30 AWG wire, or dupont-female with
      one end stripped/tinned so the ESP side needs zero solder)
- [ ] Solder per the color code in [WIRING.md](WIRING.md)
- [ ] Confirm the **100 nF** sits between VDD and GND at the chip — if none survived the
      cut, add a new one (the only component solder the whole project might need; the
      selftest passes without it, but RF reception over 10 cm of wire wants it)
- [ ] End-to-end continuity of each wire + **no short** between neighbors
- [ ] Gentle tug test on each wire
- [ ] A blob of **hot glue / epoxy** over the solder points (30 AWG rips a pad with one pull)

```
   HARVESTED MODULE                          HARNESS (5 wires, ≤10 cm)
  ┌────────────────────────────┐
  │ ╔══════╗                   │
  │ ║ ANT  ╟──[Z]──┐           │
  │ ╚══════╝  ┌────┴─────┐     │
  │           │ XN297LBW ├─ 1 CSN ── yellow → GPIO10
  │           │          ├─ 2 SCK ── green  → GPIO12
  │           │          ├─ 3 DATA ─ blue   → GPIO11
  │           │          ├─ 4 VDD ── red    → 3V3
  │           │          ├─ 7 VSS ── black  → GND
  │           └─┬──────┬─┘     │
  │          5 ─┤16 MHz├─ 6    │   100 nF stays across VDD/GND at the chip
  │            (+C1/C2)        │
  └────────────────────────────┘
```

---

## Stage 5 — Wire to the ESP32-S3

Zero solder on this side: dupont-female straight onto the header. Full pinout and the
"why these GPIOs" reasoning are in [WIRING.md](WIRING.md).

| Signal | XN297L | Wire | ESP32-S3 |
|--------|--------|------|----------|
| CSN | 1 | 🟡 | GPIO10 (FSPICS0) |
| SCK | 2 | 🟢 | GPIO12 (FSPICLK) |
| DATA | 3 | 🔵 | GPIO11 (FSPID) |
| VDD | 4 | 🔴 | 3V3 |
| VSS | 7 | ⚫ | GND |

- [ ] ESP32 **unpowered** (USB out) while connecting
- [ ] Visual check + measure 3V3↔GND on the dev board: no hard short
- [ ] Put the flex antenna as far as possible from the ESP's Wi-Fi antenna (opposite ends)
- [ ] Only then plug USB in

---

## Stage 6 — SPI bus config

ESP-IDF SPI: 3-wire, half-duplex, mode 0, **1 MHz**. See
`firmware/xn297-selftest/src/main.c` for the working `spi_bus_config` /
`spi_device_interface_config` (`SPI_DEVICE_3WIRE | SPI_DEVICE_HALFDUPLEX`,
`command_bits = 8`, `clock_speed_hz = 1 MHz`).

> Keep it at 1 MHz for *everything* until the link works — payloads don't need 4 MHz and
> it removes a variable from debugging.

---

## Stage 7 — Selftest (smoke test)

Validate the whole transplant (cut + solder + wiring + SPI) **without needing the SF900**.
The `firmware/xn297-selftest` project does this; it reads `STATUS` and echoes `TX_ADDR`:

```
   ESP32 ── R_REGISTER STATUS (0x07) ──►  XN297   →  expect 0x0E
   ESP32 ── W_REGISTER TX_ADDR: DE AD BE EF 42 ──► XN297
   ESP32 ── R_REGISTER TX_ADDR ──►  XN297         →  expect DE AD BE EF 42
```

| Result | Diagnosis |
|--------|-----------|
| STATUS `0x0E` and echo correct | ✅ **transplant OK** — from here it's all software |
| all `0x00` | DATA or SCK not arriving: check wire / solder / pin |
| all `0xFF` | DATA stuck high: check CSN and a trace↔VDD short on the cut edge |
| STATUS OK, echo garbled | clock too high or noise: shorten wires, check the 100 nF |

Once the selftest passes, run the **RSSI band scanner** (same project) — hold a button on
the SF900 nearby and you'll see energy on channels 4/29/49/79 (2404–2479 MHz), which
proves the full RF chain works. Then the **SF900 receiver** decodes actual buttons. See
[PROTOCOL.md](PROTOCOL.md) for the link parameters and [BUILD-LOG.md](BUILD-LOG.md) for
how this all came together.

---

## Antenna — decision & alternatives

| Option | Verdict | Why |
|--------|---------|-----|
| 🥇 Original flex | **default** | free, already matched to the chip by the factory `[Z]` |
| 🥈 ~31 mm monopole | honest plan B | rigid wire ~30–31 mm (λ/4 @ 2.44 GHz) soldered *after* `[Z]`, perpendicular. Slightly less range; fine for meters. Variant: u.FL/SMA pigtail + external antenna. |
| 🚫 Borrow the ESP32's Wi-Fi antenna | **don't** | it's still tied to the S3's RF front-end under the shield — two radios on one antenna detune each other; the ESP's ~20 dBm TX can desensitize/kill the XN297's LNA. 2.4 GHz doesn't travel down a plain wire either (λ ≈ 12.3 cm). Keep each antenna glued to the chip that uses it. |
