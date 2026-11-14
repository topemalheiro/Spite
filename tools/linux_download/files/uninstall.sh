#!/bin/sh
# Removes Spite as installed by install.sh. Your settings in ~/.config/spite9 and your sessions stay.
set -eu

NAME=spite9
[ -n "${HOME:-}" ] || { echo "HOME isn't set." >&2; exit 1; }
DEST=$HOME/.local/opt/$NAME
BIN=$HOME/.local/bin
DATA=${XDG_DATA_HOME:-$HOME/.local/share}

if [ "$(cd "$(dirname "$0")" && pwd)" != "$DEST" ]; then
	echo "This copy of Spite isn't installed (installed copies live in $DEST). To remove it, delete its folder." >&2
	exit 1
fi
if command -v pgrep >/dev/null 2>&1 && pgrep -f "$DEST/lib/ardour-" >/dev/null 2>&1; then
	echo "Spite is running. Close it, then run this again." >&2
	exit 1
fi

for link in "$BIN"/$NAME*; do
	[ -L "$link" ] || continue
	case "$(readlink "$link")" in
	"$DEST"/*) rm -f "$link" ;;
	esac
done

# The menu entry and icons, unless a Spite built from source has taken them over since
if grep -qs "$DEST/bin/$NAME" "$DATA/applications/$NAME.desktop"; then
	rm -f "$DATA/applications/$NAME.desktop"
	for icon in "$DATA"/icons/hicolor/*/apps/$NAME.png; do
		rm -f "$icon"
	done
fi

rm -rf "$DEST"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$DATA/applications" >/dev/null 2>&1 || true
command -v kbuildsycoca6 >/dev/null 2>&1 && kbuildsycoca6 >/dev/null 2>&1 || true
echo "Spite is uninstalled. Your settings are still in ~/.config/$NAME."
