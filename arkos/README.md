# ArkOS / dArkOS controller mapping (SF900 dongle)

When you plug the dongle into an ArkOS / dArkOS handheld it may say the controller is
"not configured". These files map it so games and the menu work. The device is:

- **Name:** `SF900 Wireless Gamepad` (two players: `Player 1` / `Player 2`)
- **USB VID:PID:** `0x303A:0x4004` (decimal `12346:16388`)
- **Buttons:** A=0, B=1, X=2, Y=3, L=4, R=5, SELECT=6, START=7 · D-pad = hat

## Easiest: the one-tap installer

Copy [`tools/Install SF900 Gamepad.sh`](tools/) into the **`tools`** folder on the
**EASYROMS** partition of your SD card (the one Windows/macOS can see). Then on the handheld
open **EmulationStation → Tools → Install SF900 Gamepad** and run it once. It installs the
RetroArch autoconfig and the SDL mapping to the right places (which live on the Linux ext4
partition your PC can't write to), so no WSL / ext4 mounting needed.

> This is the recommended path — it works for anyone, on any card, without a Linux PC.

The manual steps below do the same thing by hand if you prefer.

## 1. In-game (RetroArch)

Copy [`retroarch-autoconfig/SF900 Wireless Gamepad.cfg`](retroarch-autoconfig/) to the
RetroArch autoconfig folder on the handheld. On ArkOS that's usually:

```
/home/ark/.config/retroarch/autoconfig/udev/
```

(or `/etc/retroarch/autoconfig/udev/`). RetroArch matches by VID/PID, so it works for both
players. Re-open a game and the pad is recognized.

## 2. EmulationStation / the front-end (SDL)

The "not configured" popup comes from EmulationStation. Two options:

- **Easiest — configure it in-place:** in EmulationStation press **Start → Configure Input**
  (or hold any button when it detects a new controller) and follow the on-screen prompts.
  This writes the correct `es_input.cfg` for your exact build. This always works and is the
  recommended fix.
- **Or add the SDL mapping:** append the line in
  [`gamecontrollerdb-SF900.txt`](gamecontrollerdb-SF900.txt) to your build's
  `gamecontrollerdb.txt` (commonly `/opt/system/gamecontrollerdb.txt` or
  `/etc/gamecontrollerdb.txt`), then reboot.

## Notes

- Button numbers above come from this project's HID descriptor. If a face button feels
  swapped on your build, tweak the `input_*_btn` numbers in the RetroArch cfg — the mapping
  is A=0 B=1 X=2 Y=3 L=4 R=5 SELECT=6 START=7.
- The 2-player mode enumerates as two pads; RetroArch matches both by VID/PID with the one
  cfg. In EmulationStation each is a separate device (`js0`, `js1`).
- Reminder: holding **L + R + SELECT** ~1.5 s switches the dongle to Wi-Fi mode (it reboots).
