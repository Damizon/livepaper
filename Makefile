CC ?= gcc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
VERSION ?= 0.6.0

SRC = \
	src/main.c \
	src/backend/x11_desktop.c \
	src/core/config.c \
	src/core/paths.c \
	src/session/autostart.c \
	src/session/lock.c \
	src/session/pidfile.c \
	src/session/runtime.c \
	src/utils/string_utils.c
GUI_SRC = livepaper-gui.c
OUT = livepaper
GUI_OUT = livepaper-gui

CFLAGS ?= -Wall -O2
CPPFLAGS ?= -Iinclude
X11_LIBS = -lX11 -lXrandr -lXext
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk4)

all: $(OUT) $(GUI_OUT)

$(OUT): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) -o $(OUT) $(X11_LIBS)

$(GUI_OUT): $(GUI_SRC)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(GUI_SRC) -o $(GUI_OUT) $(GTK_LIBS)

install: all
	install -D -m 0755 $(OUT) $(DESTDIR)$(PREFIX)/bin/$(OUT)
	install -D -m 0755 $(GUI_OUT) $(DESTDIR)$(PREFIX)/bin/$(GUI_OUT)
	install -D -m 0644 packaging/livepaper.desktop $(DESTDIR)$(PREFIX)/share/applications/livepaper.desktop
	install -D -m 0644 assets/livepaper-32.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/32x32/apps/livepaper.png
	install -D -m 0644 assets/livepaper-48.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/48x48/apps/livepaper.png
	install -D -m 0644 assets/livepaper-64.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/64x64/apps/livepaper.png
	install -D -m 0644 assets/livepaper-128.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/128x128/apps/livepaper.png
	install -D -m 0644 assets/livepaper-256.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/livepaper.png
	install -D -m 0644 assets/livepaper-512.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/512x512/apps/livepaper.png

deb:
	VERSION=$(VERSION) ./packaging/build-deb.sh

clean:
	rm -f $(OUT)
	rm -f $(GUI_OUT)
	rm -f build/*.o

run: all
	./$(OUT)

.PHONY: all install deb clean run
