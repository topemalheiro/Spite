Spite @VERSION@, ready to run on 64-bit Linux, with PgD4S (plugdata for Spite) included. It installs for your user only, so it needs no root access.

**Install:**

```sh
curl -fL https://github.com/topemalheiro/Spite/releases/latest/download/Spite-linux-x86_64.tar.xz | tar -xJ
cd Spite-linux-x86_64
./install.sh
```

Then open Spite from your application menu, or run `spite9`. You can also run it straight from the extracted folder with `./bin/spite9`, without installing it. To uninstall it, run `~/.local/opt/spite9/uninstall.sh`. Your settings stay in `~/.config/spite9`.

**It needs** Ubuntu 24.04, Debian 13, a current Fedora or Arch, or another 64-bit Linux at least as new, with an X11 or Wayland desktop. Audio goes through PipeWire, JACK, ALSA or PulseAudio.

**What's inside:**
- Spite @VERSION@, built from commit @SPITE_COMMIT@.
- PgD4S's instrument and effect, built from PgD4S commit @PGD4S_COMMIT@. They're listed in Spite's plugin browser as PgD4S and PgD4S-fx.
- The Max for Live converter, as the command `spite9-m4l2pd`.

**Tested** on clean Ubuntu 24.04, Ubuntu 26.04, Debian 13, Fedora and Arch systems: installing, loading both PgD4S plugins in a session, starting the interface, updating and uninstalling.

SHA-256 of `Spite-linux-x86_64.tar.xz`: `@SHA256@`
