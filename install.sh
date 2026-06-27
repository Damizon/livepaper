#!/bin/sh
set -eu

PREFIX=${PREFIX:-/usr/local}

if [ -z "${HOME:-}" ]; then
    echo "HOME is not set." >&2
    exit 1
fi

SUDO=
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO=sudo
    else
        echo "This installer needs root privileges for system installation." >&2
        echo "Run it as root or install sudo." >&2
        exit 1
    fi
fi

install_build_deps()
{
    if command -v apt-get >/dev/null 2>&1; then
        $SUDO apt-get update
        $SUDO apt-get install -y \
            build-essential \
            pkg-config \
            libx11-dev \
            libxrandr-dev \
            libgtk-4-dev \
            mpv \
            procps \
            ffmpegthumbnailer \
            desktop-file-utils
    else
        echo "apt-get not found; install these dependencies manually:" >&2
        echo "  gcc make pkg-config libx11-dev libxrandr-dev libgtk-4-dev mpv procps ffmpegthumbnailer" >&2
    fi
}

install_build_deps

mkdir -p "$HOME/Wideo/Livepaper"
mkdir -p "$HOME/.config/livepaper"

make all
$SUDO make install PREFIX="$PREFIX"

if command -v update-desktop-database >/dev/null 2>&1; then
    $SUDO update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
fi

echo "Livepaper installed to $PREFIX."
echo "Wallpaper folder: $HOME/Wideo/Livepaper"
