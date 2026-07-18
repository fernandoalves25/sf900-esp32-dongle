# ArkOS / dArkOS controller mapping (SF900 dongle)

When you plug the dongle into an ArkOS / dArkOS handheld it may say the controller is
"not configured". These files map it so games and the menu work. The device is:

- **Name:** `DataFrog-Transplant SF900 Wireless Gamepad`
- **USB VID:PID:** `0x303A:0x4004` (decimal `12346:16388`, pinned so it never shifts)
- **Buttons:** A=0, B=1, X=2, Y=3, L=4, R=5, SELECT=6, START=7 · **D-pad = buttons 8/9/10/11**
  (up/down/left/right — ArkOS reads the D-pad as buttons, like the built-in pad, not a hat)

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

(or `/etc/retroarch/autoconfig/udev/`). RetroArch matches by VID/PID + name. Re-open a game
and the pad is recognized.

### Make it Player 2, alongside your built-in controller

So the SF900 doesn't *replace* the built-in pad, set RetroArch's **Max Users** to at least
**2** (Settings → Input, or `input_max_users = "2"` in `retroarch.cfg`). Then the built-in
controller stays Player 1 and the SF900 auto-assigns to Player 2 — both work at the same
time. With Max Users left at 1 there's only one port, so picking the SF900 there *substitutes*
the built-in one.

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
  is A=0 B=1 X=2 Y=3 L=4 R=5 SELECT=6 START=7, D-pad up/down/left/right = 8/9/10/11.
- The SF900 is a single gamepad (one `js`); the built-in pad is a separate device. Keep
  Max Users ≥ 2 so they coexist.
- Reminder: holding **L + R + SELECT** ~1.5 s switches the dongle to Wi-Fi mode (it reboots).
