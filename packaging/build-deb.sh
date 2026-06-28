#!/bin/sh
set -eu

PACKAGE=livepaper
VERSION=${VERSION:-0.5.0}
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

mkdir -p "$ROOT/usr/share/doc/$PACKAGE"
printf '%s\n' "Livepaper $VERSION" > "$ROOT/usr/share/doc/$PACKAGE/README"
chmod 0644 "$ROOT/usr/share/doc/$PACKAGE/README"

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
