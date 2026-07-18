# SF900 / SF2000 2.4 GHz controller protocol (XN297L)

The Data Frog **SF900** wireless controller (and the SF2000 / Y2 SFC family) talks to
the console over a **Panchip XN297L** 2.4 GHz transceiver — an nRF24L01-compatible chip
with an extra whitening/scramble layer and a different analog register set.

This protocol was **reverse-engineered by [axgdev/UniFrog](https://github.com/axgdev/UniFrog)**
(`src/unifrog_input_wireless.c`). It is documented here in XN297L-register terms so it
can be reimplemented on any XN297L host (this project uses an ESP32-S3). All credit for
the reverse engineering goes to that project.

Register addresses below are the raw XN297L register numbers; on the SPI wire a write is
`0x20 | reg` and a read is `0x00 | reg` (nRF24 convention).

## Link parameters

| Parameter        | Value                                  |
|------------------|----------------------------------------|
| Address width    | 5 bytes                                |
| RX_ADDR_P0       | `DC A8 F3 6B 74`                       |
| RX_ADDR_P1       | `B2 9D 59 4F E3` (2nd controller/pipe) |
| Hop channels     | `0x04, 0x1D, 0x31, 0x4F` = 2404 / 2429 / 2449 / 2479 MHz |
| Payload width    | 2 bytes                                |
| CRC              | 2 bytes (EN_CRC=1, CRCO=1)             |
| Auto-ACK         | EN_AA = `0x03` (pipes 0 & 1)           |
| Scramble         | on (DEM_CAL bit0 = 1)                  |

The receiver **hops** through the four channels: advance on every received packet, and
also after 2 consecutive empty polls, so it keeps re-syncing with the transmitter.

## Register init (the "stock" config)

Written after a soft reset (`0x53 0x5A` then `0x53 0xA5`):

| Reg  | Name        | Value / bytes            |
|------|-------------|--------------------------|
| 0x1D | FEATURE     | `0x20` (CE via SPI)      |
| 0x1F | BB_CAL      | `0A 6D 67 9C 46`         |
| 0x1E | RF_CAL      | `F6 37 5D`               |
| 0x19 | DEM_CAL     | `0x01` (SCRAMBLE_EN=1)   |
| 0x00 | CONFIG      | `0x8E` idle, `0x8F` for RX |
| 0x01 | EN_AA       | `0x03`                   |
| 0x02 | EN_RXADDR   | `0x03`                   |
| 0x03 | SETUP_AW    | `0x03` (5-byte address)  |
| 0x04 | SETUP_RETR  | `0x02`                   |
| 0x11 | RX_PW_P0    | `0x02`                   |
| 0x12 | RX_PW_P1    | `0x02`                   |
| 0x1C | DYNPD       | `0x00`                   |
| 0x06 | RF_SETUP    | `0x3F`                   |
| 0x0A | RX_ADDR_P0  | `DC A8 F3 6B 74`         |
| 0x0B | RX_ADDR_P1  | `B2 9D 59 4F E3`         |
| 0x05 | RF_CH       | start at `0x04`, then hop |

Enter RX: `CE_OFF (0xFC 00)`, flush, `CONFIG=0x8F`, wait ~10 ms, `CE_ON (0xFD 00)`.

> **Important:** the XN297L requires these calibration writes for the receiver to work
> at all. A "bare" init (no BB_CAL / RF_CAL / DEM_CAL) will read registers fine over SPI
> but never receive anything on air. All register writes happen at **≤ 1 MHz SPI** (the
> chip only accepts config writes in power-down/standby, which is clock-limited).

## Reading a packet

Poll `STATUS` (reg 0x07); bit 6 (`0x40`, RX_DR) means a packet is ready. Read 2 bytes
with the `R_RX_PAYLOAD` command (`0x61`):

```c
raw = (pkt[0] << 8) | (~pkt[1] & 0xFF);   // NOTE: second byte is inverted
```

The `raw` 16-bit value is the button bitmask. The pipe (which controller) comes from
`STATUS` bits [3:1].

## Button bitmap (of `raw`)

| Bit     | Button  |
|---------|---------|
| 0x0001  | RIGHT   |
| 0x0002  | LEFT    |
| 0x0004  | DOWN    |
| 0x0008  | UP      |
| 0x0010  | START   |
| 0x0020  | SELECT  |
| 0x0040  | B       |
| 0x0080  | A       |
| 0x0800  | L       |
| 0x1000  | R       |
| 0x2000  | Y       |
| 0x4000  | X       |
| 0x8000  | controller # (pipe indicator) |

This project maps the D-pad to a HID hat switch and A/B/X/Y/L/R/SELECT/START to HID
buttons 0–7. The mode-switch combo **L + R + SELECT** is `0x0800 | 0x1000 | 0x0020 = 0x1820`.

## Notes for reimplementers

- The controller **auto-sleeps** quickly when idle — it only transmits while buttons are
  being pressed. Keep this in mind when testing reception.
- With `EN_AA=0x03` the XN297L hardware auto-acknowledges received packets; a pure
  receiver still works because the controller broadcasts regardless.
- Multiples of 16 MHz (2400/2416/2432/2448/2464/2480) have ~2 dB worse sensitivity per
  the datasheet — the hop set avoids the worst of these.
