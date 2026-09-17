#!/bin/bash
# install.sh: Build and install Avell Control Center into Omarchy
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_ID="rafaelportomoura.avell-control-center"
DEST_DIR="$HOME/.config/omarchy/plugins/$PLUGIN_ID"

echo "==> Compilando avell-ctl..."
gcc -O2 -Wall -Wextra \
  $(pkg-config --cflags libusb-1.0 2>/dev/null || echo "-I/usr/include/libusb-1.0") \
  "$SCRIPT_DIR/bin/avell-ctl.c" \
  $(pkg-config --libs libusb-1.0 2>/dev/null || echo "-lusb-1.0") \
  -o "$SCRIPT_DIR/bin/avell-ctl"

echo "==> Criando diretório de destino: $DEST_DIR"
rm -rf "$DEST_DIR"
mkdir -p "$DEST_DIR/bin" "$DEST_DIR/scripts"

echo "==> Copiando arquivos do plugin..."
cp "$SCRIPT_DIR/manifest.json" "$DEST_DIR/"
cp "$SCRIPT_DIR/BarWidget.qml" "$DEST_DIR/"
cp "$SCRIPT_DIR/Panel.qml" "$DEST_DIR/"
cp "$SCRIPT_DIR/AvellModel.js" "$DEST_DIR/"
cp "$SCRIPT_DIR/bin/avell-ctl" "$DEST_DIR/bin/"
cp "$SCRIPT_DIR/scripts/setup-udev.sh" "$DEST_DIR/scripts/"
chmod +x "$DEST_DIR/bin/avell-ctl" "$DEST_DIR/scripts/setup-udev.sh"

echo "==> Validando plugin com o Omarchy..."
omarchy plugin validate "$DEST_DIR"

echo "==> Atualizando descoberta de plugins do Omarchy Shell..."
omarchy-shell shell rescanPlugins || true

echo ""
echo "Plugin instalado com sucesso!"
echo "Para ativar na barra do Omarchy:"
echo "  omarchy plugin enable $PLUGIN_ID right"
