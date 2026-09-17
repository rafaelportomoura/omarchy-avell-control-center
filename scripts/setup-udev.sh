#!/bin/bash
# setup-udev.sh: Install udev rule for Avell / ITE 8291 keyboard controller
set -e

RULE_FILE="/etc/udev/rules.d/99-avell-keyboard.rules"

if [ "$(id -u)" -ne 0 ]; then
    if command -v pkexec >/dev/null 2>&1; then
        exec pkexec "$0" "$@"
    elif command -v sudo >/dev/null 2>&1; then
        exec sudo "$0" "$@"
    else
        echo "Erro: Este script requer privilégios de root (sudo ou pkexec)." >&2
        exit 1
    fi
fi

cat << 'EOF' > "$RULE_FILE"
# Avell / TongFang / ITE 8291 Rev 0.03 RGB Keyboard Backlight Controller
SUBSYSTEMS=="usb", ATTRS{idVendor}=="048d", ATTRS{idProduct}=="ce00", MODE="0666", TAG+="uaccess"
KERNEL=="hidraw*", ATTRS{idVendor}=="048d", ATTRS{idProduct}=="ce00", MODE="0666", TAG+="uaccess"
EOF

echo "Regra gravada em $RULE_FILE"
udevadm control --reload-rules
udevadm trigger --subsystem-match=usb --attr-match=idVendor=048d --attr-match=idProduct=ce00 || udevadm trigger
echo "Regras udev recarregadas com sucesso!"
