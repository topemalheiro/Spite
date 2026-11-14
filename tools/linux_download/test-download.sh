#!/bin/bash
# Tests the download made by make-download.sh in clean containers, the way a user installs it.
# See tests/inside.sh for the checks.
#
#     tools/linux_download/test-download.sh [image ...]
#
# Default images: ubuntu:24.04 ubuntu:26.04 debian:trixie fedora:latest archlinux:latest
# Each container gets only what a desktop normally has (GTK 3 brings glib, cairo, pango, fonts and
# X11), plus Xvfb and ImageMagick for the interface check. The logs and a screenshot of each image
# go to $SPITE_DOWNLOAD_WORK/tests/<image>/. The exit status is 1 if any image fails.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${SPITE_DOWNLOAD_WORK:-$HOME/.cache/spite-download}
TARBALL=$WORK/out/Spite-linux-x86_64.tar.xz
[ -f "$TARBALL" ] || { echo "No download at $TARBALL: run make-download.sh first." >&2; exit 2; }
[ $# -gt 0 ] || set -- ubuntu:24.04 ubuntu:26.04 debian:trixie fedora:latest archlinux:latest

failed=0
for image in "$@"; do
	name=$(echo "$image" | tr ':/' '__')
	log=${WORK:?}/tests/${name:?}
	rm -rf "$log" && mkdir -p "$log" && chmod 777 "$log"
	case $image in
	ubuntu*|debian*) setup='apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends libgtk-3-0t64 libasound2t64 libcurl4t64 libgl1 libegl1 libusb-1.0-0 libpulse0 fonts-dejavu-core python3 xz-utils procps xvfb xauth imagemagick >/dev/null' ;;
	fedora*) setup='dnf install -y -q gtk3 alsa-lib libcurl mesa-libGL mesa-libEGL libusb1 pulseaudio-libs dejavu-sans-fonts python3 xz procps-ng xorg-x11-server-Xvfb ImageMagick findutils >/dev/null' ;;
	archlinux*) setup='pacman -Syu --noconfirm --needed -q gtk3 alsa-lib curl mesa libusb libpulse ttf-dejavu python xz procps-ng xorg-server-xvfb imagemagick >/dev/null 2>&1' ;;
	*) echo "== $image: no package list for this image (see the case in $0)" >&2; failed=1; continue ;;
	esac
	# shellcheck disable=SC2086
	docker run --rm ${DOCKER_ARGS:-} \
		-v "$TARBALL":/dl/Spite-linux-x86_64.tar.xz:ro -v "$HERE/tests":/tests:ro -v "$log":/log \
		"$image" bash -c "$setup && useradd -m tester && su tester -c 'bash /tests/inside.sh'" > "$log/result.txt" 2>&1
	status=$?
	if [ $status = 0 ]; then echo "== $image: passed"; else echo "== $image: FAILED (exit $status), see $log"; failed=1; fi
	grep -E '^(OK|FAIL)' "$log/result.txt"
done
exit $failed
