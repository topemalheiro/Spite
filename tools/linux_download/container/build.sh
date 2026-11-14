#!/bin/bash
# Runs inside the build container (started by make-download.sh), with this folder's parent mounted
# at /release and the work folder at /work.
#
#     build.sh [git ref]     default: origin/master
#
# Clones Spite from its public repository with every submodule, as a user would, builds Spite and
# PgD4S's two LV2 plugins, and leaves Spite's installed files in /work/stage. bundle.sh packs them.
set -euo pipefail
REF=${1:-origin/master}
JOBS=${JOBS:-4}
REPO=${SPITE_REPO:-https://github.com/topemalheiro/Spite.git}
export HOME=/work/home
mkdir -p "$HOME" /work/src

SRC=/work/src/Spite
if [ ! -d "$SRC/.git" ]; then
	git clone "$REPO" "$SRC"
fi
cd "$SRC"
git fetch --tags origin
git checkout -q --detach "$REF"
git submodule sync --recursive
git submodule update --init --recursive --depth 1 --jobs 4
echo "== Spite $(git describe HEAD) ($(git rev-parse --short HEAD)), PgD4S $(git -C PgD4S rev-parse --short HEAD)"

/release/container/build-aubio.sh
export PKG_CONFIG_PATH=/work/deps/lib/pkgconfig
export LD_LIBRARY_PATH=/work/deps/lib

# Spite, with the settings of install-spite.sh, plus:
#   --libjack=weak     Spite then starts on systems without JACK installed
#   --no-lrdf          see the Dockerfile
#   --no-phone-home    otherwise Spite asks ardour.org for news and shows Ardour's update notices
# A new commit gets a clean build. When only the version changes, waf relinks the program under its
# new name without recompiling the files that print the version, and it would still say the old one.
rev=$(git rev-parse HEAD)
if [ "$(cat /work/build/spite-built-commit 2>/dev/null)" != "$rev" ]; then
	rm -rf "$SRC/build" /work/build/spite-built-commit
fi
python3 ./waf configure --prefix=/opt/spite --optimize --cxx17 \
	--with-backends=jack,alsa,pulseaudio,dummy --libjack=weak --no-lrdf --no-phone-home
python3 ./waf build -j"$JOBS"
rm -rf /work/stage
python3 ./waf install --destdir=/work/stage
echo "$rev" > /work/build/spite-built-commit

# PgD4S: only the two LV2 plugins that Spite loads
cmake -S PgD4S -B /work/build/pgd4s -G Ninja -DCMAKE_BUILD_TYPE=Release -DQUICK_BUILD=1
cmake --build /work/build/pgd4s --target plugdata_LV2 plugdata_fx_LV2 -j"$JOBS"
echo "== build done"
