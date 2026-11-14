#!/bin/bash
# Runs inside the build container after build.sh (started by make-download.sh). Packs the build into
# Spite's Linux download, /work/out/Spite-linux-x86_64.tar.xz, with its checksum and release notes.
set -euo pipefail
SRC=/work/src/Spite
STAGE=/work/stage/opt/spite
NAME=Spite-linux-x86_64
OUT=/work/out
B=$OUT/$NAME
FILES=/release/files
[ -d "$STAGE/lib/spite9" ] || { echo "bundle: no build in /work/stage, run build.sh first" >&2; exit 1; }

rm -rf "${B:?}" "$OUT/$NAME.tar.xz" "$OUT/$NAME.tar.xz.sha256" "$OUT/release-notes.md" "$OUT/release.env"
mkdir -p "$B/bin" "$B/share/doc"

# Spite's installed files, one level up: lib/spite9 -> lib, share/spite9 -> share, etc/spite9 -> etc.
# Started by bin/spite9 in "bundled" mode, the program finds everything relative to its own folder.
cp -a "$STAGE/lib/spite9/." "$B/lib/"
cp -a "$STAGE/share/spite9/." "$B/share/"
cp -a "$STAGE/etc/spite9/." "$B/etc/"
rm -f "$B"/lib/*.a "$B/lib/utils/ardour-util.sh"

exe=$(cd "$B/lib" && ls | grep -E '^ardour-[0-9]+\.[0-9]+\.[0-9]+$')
version=${exe#ardour-}
sed "s|@VERSION@|$version|" "$FILES/spite9.in" > "$B/bin/spite9"
chmod 755 "$B/bin/spite9"

# The command-line tools: Lua scripting and the session utilities
make_tool() {
	sed "s|@TARGET@|$2|" "$FILES/util.in" > "$B/bin/$1"
	chmod 755 "$B/bin/$1"
}
make_tool spite9-lua lib/luasession
for util in "$B"/lib/utils/spite9-*; do
	make_tool "$(basename "$util")" "lib/utils/$(basename "$util")"
done

# PgD4S's instrument and effect go where Spite looks for the LV2 plugins it ships with. Their
# resource files are identical, so the effect's is a link to the instrument's.
cp -a "$SRC/PgD4S/Plugins/LV2/PgD4S.lv2" "$SRC/PgD4S/Plugins/LV2/PgD4S-fx.lv2" "$B/lib/LV2/"
if cmp -s "$B/lib/LV2/PgD4S.lv2/plugdata-resources.bin" "$B/lib/LV2/PgD4S-fx.lv2/plugdata-resources.bin"; then
	ln -sf ../PgD4S.lv2/plugdata-resources.bin "$B/lib/LV2/PgD4S-fx.lv2/plugdata-resources.bin"
fi

# The Max for Live converter (needs only Python 3)
install -m 755 "$SRC/tools/m4l/m4l2pd.py" "$B/bin/spite9-m4l2pd"
cp "$SRC/tools/m4l/README.md" "$B/share/doc/m4l-converter.md"

# Smaller files: drop the symbol tables of everything built here (system libraries come stripped)
find "$B" -type f -print0 | while IFS= read -r -d '' f; do
	if file -b "$f" | grep -q '^ELF.*not stripped'; then
		strip --strip-unneeded "$f"
	fi
done

# The libraries it needs that a desktop may not have. Each is used only where the system lacks it.
python3 /release/container/bundle-libs.py "$B" /work/deps/lib | tee "$OUT/bundle-libs.log"
install -m 644 "$FILES/paths.sh" "$B/lib/fallback/paths.sh"

# Where each copied library comes from, for its licence and source
{
	echo "Libraries in lib/ that come from Ubuntu 24.04 (their sources: https://packages.ubuntu.com, release noble):"
	for lib in $(grep '^  copied' "$OUT/bundle-libs.log" | awk '{print $2}'); do
		src=$(grep "^  copied $lib <- " "$OUT/bundle-libs.log" | sed 's/.* <- //')
		pkg=$(dpkg -S "$(readlink -f "$src")" 2>/dev/null | head -1 | cut -d: -f1 || true)
		[ -z "$pkg" ] && pkg=$(dpkg -S "$src" 2>/dev/null | head -1 | cut -d: -f1 || true)
		if [ -n "$pkg" ]; then
			echo "  $lib: $pkg $(dpkg-query -W -f='${Version}' "$pkg")"
		else
			echo "  $lib: aubio 0.4.9, built from https://github.com/aubio/aubio (GPL v3)"
		fi
	done
} > "$B/share/doc/bundled-libraries.txt"

# The program must report the version it's packed as (see the clean build in build.sh)
reported=$("$B/bin/spite9" --version 2>/dev/null | grep -o "^Spite[0-9][0-9.]*" || true)
if [ "$reported" != "Spite$version" ]; then
	echo "bundle: the program reports '$reported', but this is Spite$version. Remove /work/src/Spite/build and build again." >&2
	exit 1
fi

# The top level: install, uninstall, readme, licences, version
spite_commit=$(git -C "$SRC" rev-parse HEAD)
pgd4s_commit=$(git -C "$SRC/PgD4S" rev-parse HEAD)
described="Spite $version ($(git -C "$SRC" describe HEAD)), built $(date -u +%Y-%m-%d) on Ubuntu 24.04"
install -m 755 "$FILES/install.sh" "$FILES/uninstall.sh" "$B/"
sed -e "s|@VERSION@|$described|" -e "s|@SPITE_COMMIT@|$spite_commit|" -e "s|@PGD4S_COMMIT@|$pgd4s_commit|" \
	"$FILES/README.txt" > "$B/README.txt"
cp "$SRC/LICENSE" "$B/LICENSE"
cp "$SRC/PgD4S/LICENSE" "$B/share/doc/PgD4S-LICENSE"
printf '%s\nspite %s\npgd4s %s\n' "$described" "$spite_commit" "$pgd4s_commit" > "$B/VERSION"

# Owned by root in the archive, so no builder's user name ends up in it
tar -C "$OUT" --sort=name --owner=0 --group=0 --numeric-owner -cf - "$NAME" | xz -T4 -9 > "$OUT/$NAME.tar.xz"
(cd "$OUT" && sha256sum "$NAME.tar.xz" > "$NAME.tar.xz.sha256")

# The text for the release page, and what to publish it as
sed -e "s|@VERSION@|$version|g" -e "s|@SPITE_COMMIT@|$spite_commit|g" -e "s|@PGD4S_COMMIT@|$pgd4s_commit|g" \
	-e "s|@SHA256@|$(cut -d' ' -f1 "$OUT/$NAME.tar.xz.sha256")|" "$FILES/release-notes.md" > "$OUT/release-notes.md"
printf 'VERSION=%s\nTAG=v%s\nCOMMIT=%s\n' "$version" "$version" "$spite_commit" > "$OUT/release.env"

du -sh "$B" "$OUT/$NAME.tar.xz"
echo "== bundle done: $described"
