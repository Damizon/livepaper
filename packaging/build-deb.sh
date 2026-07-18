#!/bin/sh
set -eu

PACKAGE=livepaper
VERSION=${VERSION:-1.1.0}
ARCH=$(dpkg --print-architecture)
ROOT=build/deb-root
OUTDIR=build/packages
CONTROL="$ROOT/DEBIAN/control"
SUBSTVARS="$ROOT/DEBIAN/substvars"
SHLIBDEPS_DIR=build/shlibdeps
REPO_DIR=$(pwd)

rm -rf "$ROOT"
rm -rf "$SHLIBDEPS_DIR"
mkdir -p "$ROOT/DEBIAN" "$OUTDIR" "$SHLIBDEPS_DIR/debian"

make all
make install DESTDIR="$ROOT" PREFIX=/usr

install -D -m 0644 assets/livepaper-32.png "$ROOT/usr/share/icons/hicolor/32x32/apps/livepaper.png"
install -D -m 0644 assets/livepaper-48.png "$ROOT/usr/share/icons/hicolor/48x48/apps/livepaper.png"
install -D -m 0644 assets/livepaper-64.png "$ROOT/usr/share/icons/hicolor/64x64/apps/livepaper.png"
install -D -m 0644 assets/livepaper-128.png "$ROOT/usr/share/icons/hicolor/128x128/apps/livepaper.png"
install -D -m 0644 assets/livepaper-256.png "$ROOT/usr/share/icons/hicolor/256x256/apps/livepaper.png"
install -D -m 0644 assets/livepaper-512.png "$ROOT/usr/share/icons/hicolor/512x512/apps/livepaper.png"

mkdir -p "$ROOT/usr/share/doc/$PACKAGE"
printf '%s\n' "Livepaper $VERSION" > "$ROOT/usr/share/doc/$PACKAGE/README"
chmod 0644 "$ROOT/usr/share/doc/$PACKAGE/README"

cat > "$ROOT/DEBIAN/postinst" <<'EOF_POSTINST'
#!/bin/sh
set -e

create_livepaper_dir() {
    user_name=$1
    home_dir=$2
    group_id=$3
    videos_dir=

    if [ -f "$home_dir/.config/user-dirs.dirs" ]; then
        videos_dir=$(sed -n 's/^XDG_VIDEOS_DIR="\([^"]*\)".*/\1/p' "$home_dir/.config/user-dirs.dirs" | head -n 1)
        case "$videos_dir" in
            "\$HOME"/*)
                videos_dir="$home_dir/$(printf '%s\n' "$videos_dir" | sed 's|^\$HOME/||')"
                ;;
            "\${HOME}"/*)
                videos_dir="$home_dir/$(printf '%s\n' "$videos_dir" | sed 's|^\${HOME}/||')"
                ;;
            "$home_dir"|"\$HOME"|"\${HOME}"|"")
                videos_dir=
                ;;
            /*)
                ;;
            *)
                videos_dir="$home_dir/$videos_dir"
                ;;
        esac
    fi

    if [ -z "$videos_dir" ]; then
        videos_dir="$home_dir/Videos"
    fi

    mkdir -p "$videos_dir/Livepaper" || true
    chown "$user_name:$group_id" "$videos_dir" "$videos_dir/Livepaper" 2>/dev/null || true
}

if command -v getent >/dev/null 2>&1; then
    getent passwd | while IFS=: read -r user_name _ uid gid _ home_dir _; do
        if [ "$uid" -ge 1000 ] 2>/dev/null && [ -d "$home_dir" ]; then
            create_livepaper_dir "$user_name" "$home_dir" "$gid"
        fi
    done
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi

exit 0
EOF_POSTINST
chmod 0755 "$ROOT/DEBIAN/postinst"

cat > "$SHLIBDEPS_DIR/debian/control" <<EOF_SHLIBDEPS_CONTROL
Source: $PACKAGE
Section: utils
Priority: optional
Maintainer: Livepaper Maintainers <maintainers@example.invalid>
Standards-Version: 4.6.2

Package: $PACKAGE
Architecture: any
Depends: \${shlibs:Depends}
Description: Video wallpaper manager for Cinnamon/Nemo desktops
 Livepaper embeds a looping video player behind the desktop icons.
EOF_SHLIBDEPS_CONTROL

(
    cd "$SHLIBDEPS_DIR"
    dpkg-shlibdeps -T"$REPO_DIR/$SUBSTVARS" \
        "$REPO_DIR/$ROOT/usr/bin/livepaper" \
        "$REPO_DIR/$ROOT/usr/bin/livepaper-gui"
)

SHLIBS_DEPENDS=$(sed -n 's/^shlibs:Depends=//p' "$SUBSTVARS")
RUNTIME_DEPENDS="mpv, procps, ffmpegthumbnailer"

cat > "$CONTROL" <<EOF_CONTROL
Package: $PACKAGE
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Livepaper Maintainers <maintainers@example.invalid>
Depends: $SHLIBS_DEPENDS, $RUNTIME_DEPENDS
Description: Video wallpaper manager for Cinnamon/Nemo desktops
 Livepaper embeds a looping video player behind the desktop icons and
 includes a GTK interface for selecting wallpapers from the user's video
 wallpaper folder.
EOF_CONTROL
chmod 0644 "$CONTROL"
find "$ROOT" -type d -exec chmod 0755 {} +

dpkg-deb --root-owner-group --build "$ROOT" "$OUTDIR/${PACKAGE}_${VERSION}_${ARCH}.deb"
printf '%s\n' "$OUTDIR/${PACKAGE}_${VERSION}_${ARCH}.deb"
