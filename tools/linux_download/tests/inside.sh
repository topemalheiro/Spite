#!/bin/bash
# Runs as a normal user inside a clean container (started by test-download.sh): installs the
# download the way a user would, then checks it. Its logs go to /log.
set -uo pipefail
fail=0
step() { echo "== $*"; }
ok() { echo "OK   $*"; }
bad() { echo "FAIL $*"; fail=1; }

step "install"
cd ~ && tar -xJf /dl/Spite-linux-x86_64.tar.xz && cd Spite-linux-x86_64 || { bad "extract"; exit 1; }
if ./install.sh > /log/install.log 2>&1; then ok "install.sh"; else bad "install.sh: $(tail -3 /log/install.log)"; fi
for f in ~/.local/bin/spite9 ~/.local/share/applications/spite9.desktop ~/.local/share/icons/hicolor/48x48/apps/spite9.png; do
	if [ -e "$f" ]; then ok "exists: $f"; else bad "missing: $f"; fi
done

step "every program and plugin finds its libraries"
cd ~/.local/opt/spite9 || exit 1
. lib/fallback/paths.sh
libpath="$PWD/lib$(spite_fallback_path "$PWD")"
echo "bundled libraries used here: $(spite_fallback_path "$PWD" | tr ':' '\n' | sed 's|.*/||' | tr '\n' ' ')"
: > /log/ldd-missing.txt
n=0
while IFS= read -r -d '' f; do
	head -c4 "$f" 2>/dev/null | grep -q ELF || continue
	n=$((n + 1))
	LD_LIBRARY_PATH=$libpath ldd "$f" 2>&1 | grep 'not found' | sed "s|^|$f: |" >> /log/ldd-missing.txt
done < <(find . -type f -print0)
if [ -s /log/ldd-missing.txt ]; then bad "$(sort -u -t: -k2 /log/ldd-missing.txt | head -20)"; else ok "$n programs and libraries, nothing missing"; fi

step "the program reports the download's version"
packed=$(head -1 ~/.local/opt/spite9/VERSION | grep -o '^Spite [0-9.]*' | tr -d ' ')
reported=$(~/.local/bin/spite9 --version 2>/dev/null | grep -o '^Spite[0-9][0-9.]*')
if [ -n "$packed" ] && [ "$reported" = "$packed" ]; then ok "$reported"; else bad "packed as '$packed', but the program says '$reported'"; fi

step "PgD4S in a session, without a screen"
if TEST_SESSION_DIR=$HOME/test-session timeout 180 ~/.local/bin/spite9-lua /tests/headless.lua > /log/headless.log 2>&1 \
	&& grep -q '^STEP DONE' /log/headless.log; then
	ok "$(grep '^STEP loaded' /log/headless.log | tr '\n' ' ')"
else
	bad "headless: $(grep -v '^STEP' /log/headless.log | tail -5)"
fi

step "the Max for Live converter runs"
~/.local/bin/spite9-m4l2pd > /log/m4l.log 2>&1
st=$?
if [ $st = 2 ] && grep -q -- '--install' /log/m4l.log; then ok "spite9-m4l2pd prints its usage"; else bad "spite9-m4l2pd (exit $st): $(tail -2 /log/m4l.log)"; fi

step "the interface starts, on a virtual screen"
Xvfb :99 -screen 0 1600x1000x24 > /log/xvfb.log 2>&1 &
sleep 2
DISPLAY=:99 ~/.local/bin/spite9 > /log/gui.log 2>&1 &
pid=$!
sleep 40
DISPLAY=:99 import -window root /log/screen.png 2>/dev/null
if kill -0 $pid 2>/dev/null; then
	ok "Spite still running after 40 s (screenshot: screen.png)"

	step "install.sh and uninstall.sh refuse while Spite runs"
	if (cd ~/Spite-linux-x86_64 && ./install.sh) > /log/running-install.log 2>&1; then bad "install.sh replaced a running Spite"; else ok "install.sh refused"; fi
	if ~/.local/opt/spite9/uninstall.sh > /log/running-uninstall.log 2>&1; then bad "uninstall.sh removed a running Spite"; else ok "uninstall.sh refused"; fi
	kill $pid; sleep 2; kill -9 $pid 2>/dev/null
	sleep 1
else
	bad "Spite exited: $(tail -5 /log/gui.log)"
fi

step "install again over it, as an update"
cd ~/Spite-linux-x86_64 || exit 1
if ./install.sh > /log/update.log 2>&1 && [ -x ~/.local/opt/spite9/bin/spite9 ]; then ok "second install.sh"; else bad "second install.sh: $(tail -3 /log/update.log)"; fi

step "install.sh leaves a Spite built from source alone"
mkdir -p ~/.local/lib/spite9
if ./install.sh > /log/refuse.log 2>&1; then bad "installed over a source install without --force"; else ok "refused without --force"; fi
if ./install.sh --force > /log/force.log 2>&1; then ok "--force installs"; else bad "--force: $(tail -2 /log/force.log)"; fi
rmdir ~/.local/lib/spite9

step "uninstall"
if ~/.local/opt/spite9/uninstall.sh > /log/uninstall.log 2>&1 && [ ! -e ~/.local/opt/spite9 ] && [ ! -e ~/.local/bin/spite9 ] \
	&& [ ! -e ~/.local/share/applications/spite9.desktop ]; then
	ok "uninstall.sh"
else
	bad "uninstall: $(tail -2 /log/uninstall.log)"
fi
if [ $fail = 0 ]; then echo "== ALL OK"; else echo "== FAILURES"; fi
exit $fail
