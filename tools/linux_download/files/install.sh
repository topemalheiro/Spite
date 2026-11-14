#!/bin/sh
# Installs Spite for your user only: no root, and nothing outside your home folder.
#
#   ./install.sh           install, or update an earlier install
#   ./install.sh --force   install even though a Spite built from source is installed here
#
# Spite goes into ~/.local/opt/spite9, its commands into ~/.local/bin, and it gets an entry in
# your application menu. Your settings stay in ~/.config/spite9.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
NAME=spite9
[ -n "${HOME:-}" ] || { echo "HOME isn't set, so there's no home folder to install into." >&2; exit 1; }
DEST=$HOME/.local/opt/$NAME
BIN=$HOME/.local/bin
DATA=${XDG_DATA_HOME:-$HOME/.local/share}

if [ "$(id -u)" = 0 ]; then
	echo "Run this as yourself, not as root or with sudo: Spite installs into your home folder." >&2
	exit 1
fi
if [ "$HERE" = "$DEST" ]; then
	echo "This copy is already the installed one, in $DEST." >&2
	exit 1
fi
if [ ! -x "$HERE/bin/$NAME" ]; then
	echo "Run this from the extracted Spite-linux-x86_64 folder: $HERE/bin/$NAME is missing." >&2
	exit 1
fi

# A Spite built from source (install-spite.sh) uses the same command and menu entry
if [ -d "$HOME/.local/lib/$NAME" ] && [ "${1:-}" != "--force" ]; then
	echo "A Spite built from its source code is already installed here (by install-spite.sh, in ~/.local/lib/$NAME)." >&2
	echo "This download would take over its $NAME command and its menu entry." >&2
	echo "To install it anyway, run: ./install.sh --force" >&2
	exit 1
fi

if command -v pgrep >/dev/null 2>&1 && pgrep -f "$DEST/lib/ardour-" >/dev/null 2>&1; then
	echo "Spite is running. Close it, then run this again." >&2
	exit 1
fi

echo "Installing Spite into $DEST ..."
mkdir -p "$HOME/.local/opt"
rm -rf "$DEST.new"
cp -a "$HERE/." "$DEST.new"
rm -f "$DEST.new/install.sh"
rm -rf "$DEST"
mv "$DEST.new" "$DEST"

mkdir -p "$BIN"
for tool in "$DEST"/bin/*; do
	ln -sf "$tool" "$BIN/$(basename "$tool")"
done

for size in 16 22 32 48 256 512; do
	mkdir -p "$DATA/icons/hicolor/${size}x${size}/apps"
	cp "$DEST/share/resources/Spite-icon_${size}px.png" "$DATA/icons/hicolor/${size}x${size}/apps/$NAME.png"
done

mkdir -p "$DATA/applications"
cat > "$DATA/applications/$NAME.desktop" <<DESKTOP
[Desktop Entry]
Name=Spite
GenericName=Digital Audio Workstation
Comment=Spite Digital Audio Workstation
Exec="$DEST/bin/$NAME" %f
Icon=$NAME
Terminal=false
MimeType=application/x-ardour;
Type=Application
Categories=AudioVideo;Audio;AudioEditing;X-Recorders;X-Multitrack;X-Jack;
StartupWMClass=Spite
DESKTOP
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$DATA/applications" >/dev/null 2>&1 || true
command -v kbuildsycoca6 >/dev/null 2>&1 && kbuildsycoca6 >/dev/null 2>&1 || true

echo
echo "Spite is installed. Open it from your application menu, or run: $NAME"
case ":$PATH:" in
*":$BIN:"*) ;;
*) echo "($BIN isn't on your PATH, so run it as $BIN/$NAME, or add that folder to your PATH.)" ;;
esac
echo "You can delete this folder now: $HERE"
echo "To uninstall Spite later: $DEST/uninstall.sh"
