#!/usr/bin/env bash
# Builds Spite and installs it for the current user, separate from any Ardour install.
#
#     ./install-spite.sh            configure (if needed), build and install
#     ./install-spite.sh --rebuild  configure again from scratch, then build and install
#
# Everything goes under ~/.local: the launcher is ~/.local/bin/spite<version>, the program
# and its data live in ~/.local/lib/spite<version> and ~/.local/share/spite<version>, and
# Spite keeps its own settings in ~/.config/spite<version>. It also adds a "Spite" entry
# with the Spite icon to the application menu.

set -euo pipefail

cd "$(dirname "$0")"
PREFIX="$HOME/.local"

# GCC 16 defaults to a newer C++ standard than this code base expects, hence --cxx17.
if [ "${1:-}" = "--rebuild" ] || [ ! -f build/c4che/_cache.py ]; then
    python3 ./waf configure --prefix="$PREFIX" --optimize --cxx17 \
        --with-backends=jack,alsa,pulseaudio,dummy
fi

python3 ./waf build -j "$(nproc)"
python3 ./waf install

# The installed launcher is named after the program and its major version, e.g. spite9.
name=$(ls "$PREFIX/bin" | grep -E '^spite[0-9]+$' | sort -V | tail -1)
if [ -z "$name" ]; then
    echo "install-spite: no spite launcher found in $PREFIX/bin" >&2
    exit 1
fi

# Menu icon in every size the desktop may ask for.
for size in 16 22 32 48 256 512; do
    mkdir -p "$PREFIX/share/icons/hicolor/${size}x${size}/apps"
    cp "gtk2_ardour/resources/Spite-icon_${size}px.png" "$PREFIX/share/icons/hicolor/${size}x${size}/apps/$name.png"
done

# Menu entry. StartupWMClass lets the taskbar match Spite's windows to this entry.
mkdir -p "$PREFIX/share/applications"
cat > "$PREFIX/share/applications/$name.desktop" <<EOF
[Desktop Entry]
Name=Spite
GenericName=Digital Audio Workstation
Comment=Spite Digital Audio Workstation
Exec=$PREFIX/bin/$name %f
Icon=$name
Terminal=false
MimeType=application/x-ardour;
Type=Application
Categories=AudioVideo;Audio;AudioEditing;X-Recorders;X-Multitrack;X-Jack;
StartupWMClass=Spite
EOF

# Refresh the menu where the tools exist. (No GTK icon cache here: a cache in ~/.local
# would hide icons that other programs add later without updating it.)
command -v update-desktop-database >/dev/null && update-desktop-database "$PREFIX/share/applications" || true
command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true

echo "Spite is installed: run $name, or open Spite from the application menu."
