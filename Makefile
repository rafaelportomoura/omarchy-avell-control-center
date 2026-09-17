CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LIBS = $(shell pkg-config --libs libusb-1.0 2>/dev/null || echo "-lusb-1.0")
INCS = $(shell pkg-config --cflags libusb-1.0 2>/dev/null || echo "-I/usr/include/libusb-1.0")

TARGET = bin/avell-ctl
SRC = bin/avell-ctl.c

PLUGIN_ID = rafaelportomoura.avell-control-center
DEST_DIR = $(HOME)/.config/omarchy/plugins/$(PLUGIN_ID)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INCS) $< $(LIBS) -o $@

clean:
	rm -f $(TARGET)

install: $(TARGET)
	@echo "Installing plugin to $(DEST_DIR)..."
	mkdir -p $(DEST_DIR)
	cp -r manifest.json BarWidget.qml Panel.qml bin scripts $(DEST_DIR)/
	@echo "Plugin installed! Running omarchy plugin validate..."
	omarchy plugin validate $(DEST_DIR)
	@echo "Rescanning Omarchy plugins..."
	omarchy-shell shell rescanPlugins || true

setup-udev:
	./scripts/setup-udev.sh

.PHONY: all clean install setup-udev
