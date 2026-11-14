#!/bin/bash
# Builds Spite's Linux download: Spite and PgD4S's LV2 plugins, compiled in an Ubuntu 24.04
# container and packed with an installer into Spite-linux-x86_64.tar.xz. See README.md here.
#
#     tools/linux_download/make-download.sh [git ref]     build and pack; default ref: origin/master
#     tools/linux_download/make-download.sh --pack        pack the last build again, without compiling
#
# It builds what the public repository has at that ref, not your working copy, so a download
# always matches a published commit. It needs Docker. The result goes to $SPITE_DOWNLOAD_WORK/out.
#
# Environment:
#   SPITE_DOWNLOAD_WORK   where the clone, the build and the result are kept between runs
#                         (default: ~/.cache/spite-download)
#   JOBS                  parallel compile jobs (default: half the cores; one job can use over 1 GB)
#   DOCKER_ARGS           extra arguments for docker run, for example "--cpus 12 --memory 22g"
#   SPITE_REPO            the repository to clone (default: https://github.com/topemalheiro/Spite.git)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${SPITE_DOWNLOAD_WORK:-$HOME/.cache/spite-download}
JOBS=${JOBS:-$(( ($(nproc) + 1) / 2 ))}
IMAGE=spite-download-builder:ubuntu24.04

mkdir -p "$WORK"
docker build -q -t "$IMAGE" "$HERE" > /dev/null
run() {
	# shellcheck disable=SC2086
	docker run --rm --user "$(id -u):$(id -g)" -e JOBS="$JOBS" -e SPITE_REPO ${DOCKER_ARGS:-} \
		-v "$HERE":/release:ro -v "$WORK":/work "$IMAGE" "$@"
}

if [ "${1:-}" != "--pack" ]; then
	run /release/container/build.sh "${1:-origin/master}"
fi
run /release/container/bundle.sh
echo "The download: $WORK/out/Spite-linux-x86_64.tar.xz"
echo "Test it with: $HERE/test-download.sh"
