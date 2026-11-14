Spite for Linux (64-bit, x86_64)
================================

Install it for your user, with no root needed:

    ./install.sh

Then open Spite from your application menu, or run spite9. You can delete this folder afterwards.
To remove Spite later: ~/.local/opt/spite9/uninstall.sh

Or run it straight from this folder, without installing: ./bin/spite9

It needs Ubuntu 24.04, Debian 13, a current Fedora or Arch, or another 64-bit Linux at least as new,
with an X11 or Wayland desktop. Audio goes through PipeWire, JACK, ALSA or PulseAudio.

PgD4S (plugdata for Spite) is included: its instrument and effect show in Spite's plugin browser as
PgD4S and PgD4S-fx. To turn Max for Live devices into PgD4S devices (this needs Python 3):

    spite9-m4l2pd --install ~/Music/Devices/*.amxd

then restart Spite and look for presets named "M4L: <device name>". Details: share/doc/m4l-converter.md

Version: @VERSION@

Source code
-----------
Spite is free software under the GNU General Public License, version 3 (see LICENSE). This build
comes from https://github.com/topemalheiro/Spite at commit @SPITE_COMMIT@,
and was made with the scripts in that repository's tools/linux_download folder.
PgD4S comes from https://github.com/topemalheiro/PgD4S at commit @PGD4S_COMMIT@.
It's GPL v3 too (share/doc/PgD4S-LICENSE), and its compiled plugins also follow the GNU Affero General
Public License v3 of the JUCE framework.
Spite is built on Ardour (https://ardour.org). The other libraries in lib/ are listed, with the
Ubuntu 24.04 packages whose sources they come from, in share/doc/bundled-libraries.txt.
