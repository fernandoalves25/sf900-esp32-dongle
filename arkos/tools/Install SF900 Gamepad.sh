#!/bin/bash
#
# Install SF900 Gamepad mapping  (SF900 ESP32 dongle)
# https://github.com/fernandoalves25/sf900-esp32-dongle
#
# Drop this file into the EASYROMS "tools" folder, then run it once from
# EmulationStation -> Tools. It installs a RetroArch autoconfig JUST for the SF900
# (matched by its USB id 0x303A:0x4004), so the pad is mapped in games.
#
# It is 100% ADDITIVE: it only writes one new per-device profile. It does NOT touch
# the handheld's built-in controller, es_input.cfg, or any shared file.

if [ "$(id -u)" -ne 0 ]; then
    exec sudo -- "$0" "$@"
fi

CURR_TTY="/dev/tty1"
printf "\033c" > "$CURR_TTY"
say() { echo "  $*" > "$CURR_TTY"; }

# ArkOS user is 'ark'. Resolve its home explicitly (running as root, $HOME=/root).
ARK_HOME="$(getent passwd ark | cut -d: -f6)"
[ -z "$ARK_HOME" ] && ARK_HOME="/home/ark"
DEST="$ARK_HOME/.config/retroarch/autoconfig/udev"

say "=== SF900 Gamepad mapping installer ==="
say ""

if [ ! -d "$DEST" ]; then
    say "RetroArch autoconfig folder not found at:"
    say "  $DEST"
    say "Is this ArkOS/dArkOS? Aborting (nothing changed)."
    sleep 6; printf "\033c" > "$CURR_TTY"; exit 1
fi

cat > "$DEST/SF900 Wireless Gamepad.cfg" <<'EOF'
input_driver = "udev"
input_device = "SF900 Wireless Gamepad"
input_vendor_id = "12346"
input_product_id = "16388"
input_b_btn = "1"
input_a_btn = "0"
input_y_btn = "3"
input_x_btn = "2"
input_l_btn = "4"
input_r_btn = "5"
input_select_btn = "6"
input_start_btn = "7"
input_up_btn = "h0up"
input_down_btn = "h0down"
input_left_btn = "h0left"
input_right_btn = "h0right"
input_enable_hotkey_btn = "6"
input_menu_toggle_btn = "7"
input_exit_emulator_btn = "7"
input_save_state_btn = "5"
input_load_state_btn = "4"
EOF
chown ark:ark "$DEST/SF900 Wireless Gamepad.cfg" 2>/dev/null

say "Installed (additive, SF900 only):"
say "  $DEST/SF900 Wireless Gamepad.cfg"
say ""
say "The built-in controller was NOT touched."
say ""
say "In games the SF900 is now mapped. In EmulationStation, if a"
say "'configure input' popup appears for it, either skip it or run"
say "Start -> Configure Input once - it ADDS the SF900, it won't"
say "remove your built-in pad."
say ""
say "Closing in 7 seconds..."
sleep 7
printf "\033c" > "$CURR_TTY"
exit 0
