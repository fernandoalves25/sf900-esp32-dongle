#!/bin/bash
#
# Install SF900 Gamepad mapping  (SF900 ESP32 dongle)
# https://github.com/fernandoalves25/sf900-esp32-dongle
#
# Drop this file into the EASYROMS "tools" folder, then run it once from
# EmulationStation -> Tools. It installs the RetroArch autoconfig (and, if found,
# an SDL gamecontrollerdb line) so the "controller not configured" popup goes away
# and the buttons are mapped. Matches by USB VID:PID 0x303A:0x4004.

if [ "$(id -u)" -ne 0 ]; then
    exec sudo -- "$0" "$@"
fi

CURR_TTY="/dev/tty1"
printf "\033c" > "$CURR_TTY"
echo "" > "$CURR_TTY"
say() { echo "  $*" > "$CURR_TTY"; }

say "=== SF900 Gamepad mapping installer ==="
say ""

# ---- 1) RetroArch autoconfig (fixes in-game controls) ----------------------
RA_CFG="/home/ark/.config/retroarch/retroarch.cfg"
AUTODIR="$(grep -m1 joypad_autoconfig_dir "$RA_CFG" 2>/dev/null | cut -d'"' -f2)"
[ -z "$AUTODIR" ] && AUTODIR="/home/ark/.config/retroarch/autoconfig"

install_ra() {
    local dir="$1"
    mkdir -p "$dir"
    cat > "$dir/SF900 Wireless Gamepad.cfg" <<'EOF'
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
input_state_slot_increase_btn = "h0right"
input_state_slot_decrease_btn = "h0left"
EOF
    chown -R ark:ark "$dir" 2>/dev/null
}

install_ra "$AUTODIR"
install_ra "$AUTODIR/udev"   # some builds look in a driver subfolder
say "RetroArch autoconfig installed:"
say "  $AUTODIR"

# ---- 2) SDL gamecontrollerdb (helps EmulationStation) ----------------------
GUID="030000003a3000000440000000010000"
LINE="$GUID,SF900 Wireless Gamepad,a:b1,b:b0,x:b3,y:b2,back:b6,start:b7,leftshoulder:b4,rightshoulder:b5,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,platform:Linux,"
DB="$(ls /opt/system/gamecontrollerdb.txt /etc/gamecontrollerdb.txt \
        /usr/share/emulationstation/resources/gamecontrollerdb.txt 2>/dev/null | head -1)"
if [ -n "$DB" ]; then
    if ! grep -q "^$GUID" "$DB" 2>/dev/null; then
        echo "$LINE" >> "$DB"
        say "SDL mapping added to: $DB"
    else
        say "SDL mapping already present in: $DB"
    fi
else
    say "(no gamecontrollerdb.txt found - skipping SDL step)"
fi

say ""
say "Done. If EmulationStation still shows 'not configured', open"
say "Start -> Configure Input in EmulationStation once (that always works)."
say ""
say "Press any key / button to exit..."
read -n 1 -s < "$CURR_TTY"
printf "\033c" > "$CURR_TTY"
