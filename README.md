# Spite

An open source DAW to finally unite sound engineers, music and audiovisual artists, and programmers as one.

## About

Spite is a clone of [Ardour](https://ardour.org/), not a fork meant to track it closely. I'm the maintainer, and I'll contribute to it extensively from now on. The changes are big enough that Spite will go its own way:

- **A new look.** A slicker, modern, flat art style inspired by plugdata, in place of Ardour's GTK2 interface.
- **Compatibility with the tools people already use.** Running Max for Live devices in Spite, so people only need a Max licence. Later, turning Max patches and devices into plugdata devices with Max-to-Pd transpilers and Cyclone (Pd objects cloned from Max, already included in plugdata), so anyone can make and share them without a licence.
- **Quality-of-life work.** Shortcuts and workflow improvements throughout.

## Progress so far

Spite started from Ardour 9 on October 27, 2026. What's done since:

- **Its own identity.** A new name, logo and launcher. `./install-spite.sh` installs it under `~/.local`, apart from any Ardour, with its own settings.
- **plugdata inside.** plugdata, the visual patching environment based on Pure Data, is part of the source as a submodule. Spite's plugdata fork adds Detective Mode: messages travel along the cables as bubbles, in the order they run and at an adjustable speed, so you can debug a patch by watching it.
- **Window and panel shortcuts.** Ctrl+Alt+F makes the focused page fullscreen, detached windows included. Ctrl+Alt+L shows or hides its bottom panel, like Live's detail view.
- **A plugin browser on either side.** The Cue, Edit and Mixer pages each have a browser with favourite, recent and searched plugins and their presets. Drag a plugin onto a strip's devices, or double-click it to add it to the selected tracks. On the Cue page, dropping an instrument on the empty strip area makes a MIDI track with it. Ctrl+Alt+B shows or hides the browser, and a setting puts it on the left, like Live's, or on the right.
- **Action macros.** A Macros menu and a Macro Editor. A macro runs a chain of actions from a key, the menu or a control surface, and you can record one just by doing the actions. There's no limit on how many, and they come in sets you can switch between. Every existing shortcut is listed and editable in the same place. A key can work everywhere or on one page only, and a macro can cycle, running one step per press, so a single key can step through pages or layouts.
- **A Cue page closer to a live setup.** Scenes are numbered 1, 2, 3 instead of lettered A to P. The master bus has its own strip on the right with the scene buttons, like Live's Main track. Strips can hide their device lists, clips take their track's colour unless given their own, and the bottom panel says "No clip selected." instead of staying blank.
- **Scripting clips.** Lua scripts can load clips into a track's slots (`Route:triggerbox()`, `TriggerBox:set_from_path()`), the first step towards building ready-to-play sets without the GUI.

## Roadmap

Most of it is about live performance on the Cue page, Spite's counterpart to Live's Session View.

**Next:**

- **Device view (in progress).** The selected track's devices as a chain across the bottom panel, like Live's, with Shift+Tab switching between the clip and its devices. Devices become modules that collapse and enlarge.
- **Custom views.** A Views menu to save and edit views (which panels, sidebars, strips and device layout each page shows) and to switch or cycle between them from a key or a control surface.
- **As many scenes as a set needs.** The letters are gone. Next, a set stops being capped at 16 scenes.

**After that:**

- **A modular rack view.** Devices laid out and stacked like Live's racks, on the Cue page and off by default.
- **An automation view tied to step sequencers.**
- **Presets within reach.** Preset slots beside each device that a control-surface button can fire, and a coloured box showing what a control surface has mapped.
- **Macro knobs.** One knob driving many parameters, each with its own range and curve.
- **Knobs that offset automation.** Recorded modulation (generative patches, LFOs and the modulators driving them) plays back from automation lines, and a control surface offsets it live without switching it off.
- **Sequencer control surfaces** that edit the MIDI notes and automation in Cue clips directly, with scripts that make other controllers, like the APC40 MKII, act like one.
- **More windows.** Multiple windows per display, and a MIDI editor for several clips and automation lines at once.
- **Colour coding.** A colour set per track or group of tracks, with hues of it for each device section.
- **Preset morphing.** Mappings and setups saved as presets and morphed between, on XY pads or pads with more corners.
- **Ready-to-play sets.** Cue page sets with the clips, devices, views and colours already laid out, for different kinds of music.

<img width="871" height="327" alt="image" src="https://github.com/user-attachments/assets/9426b204-b89a-4edb-8d80-2b9f8976eefd" />

## Build and run

Spite is written in C++17 on Ardour's code base, with Ardour's own copy of the GTK2 toolkit (in `libs/tk`), the waf build system and Lua scripting. plugdata, in the `plugdata` folder, is a separate C++ project built with CMake, on JUCE and Pure Data. Spite builds the way Ardour does, so Ardour's guides at <https://ardour.org/development.html> apply too.

Install git and your system's build dependencies first, as shown for each system below. Then clone the repository. Spite reads its version from git, so clone it instead of downloading a ZIP, and don't make a shallow clone:

```sh
git clone https://github.com/topemalheiro/Spite.git
cd Spite
```

Spite builds without plugdata. To get plugdata as well, add `--recurse-submodules` to the clone, or run `git submodule update --init --recursive` later. plugdata and its libraries take about 2 GB.

The first build compiles about 1,800 files: a few minutes on a many-core desktop, half an hour or more on a laptop. Later builds only recompile what changed.

### Linux (tested on Arch Linux and Debian 13)

Arch Linux (with PipeWire, put `pipewire-jack` in place of `jack2`):

```sh
sudo pacman -S --needed base-devel git python pkgconf boost glib2 glibmm libsigc++ cairo cairomm pango pangomm fontconfig libpng libjpeg-turbo libx11 libxext libxrandr libxinerama libxi libsndfile flac libogg libsamplerate fftw curl libarchive liblo taglib vamp-plugin-sdk rubberband aubio libxml2 liblrdf lv2 serd sord sratom lilv alsa-lib libpulse jack2 libusb systemd-libs libwebsockets dbus readline
```

Debian 13 or Ubuntu 24.04:

```sh
sudo apt update
sudo apt install build-essential git python3 pkgconf libboost-dev libglib2.0-dev libglibmm-2.4-dev libsigc++-2.0-dev libcairo2-dev libcairomm-1.0-dev libpango1.0-dev libpangomm-1.4-dev libfontconfig-dev libpng-dev libjpeg-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxi-dev libsndfile1-dev libflac-dev libogg-dev libsamplerate0-dev libfftw3-dev libcurl4-openssl-dev libarchive-dev liblo-dev libtag1-dev vamp-plugin-sdk librubberband-dev libaubio-dev libxml2-dev liblrdf0-dev lv2-dev libserd-dev libsord-dev libsratom-dev liblilv-dev libasound2-dev libpulse-dev libjack-jackd2-dev libusb-1.0-0-dev libudev-dev libwebsockets-dev libdbus-1-dev libreadline-dev
```

Fedora:

```sh
sudo dnf install gcc-c++ git python3 boost-devel libjpeg-devel fontconfig-devel libXi-devel libXinerama-devel 'pkgconfig(alsa)' 'pkgconfig(aubio)' 'pkgconfig(cairo)' 'pkgconfig(cairomm-1.0)' 'pkgconfig(dbus-1)' 'pkgconfig(fftw3f)' 'pkgconfig(flac)' 'pkgconfig(giomm-2.4)' 'pkgconfig(glib-2.0)' 'pkgconfig(glibmm-2.4)' 'pkgconfig(jack)' 'pkgconfig(libarchive)' 'pkgconfig(libcurl)' 'pkgconfig(liblo)' 'pkgconfig(libpulse)' 'pkgconfig(libusb-1.0)' 'pkgconfig(libwebsockets)' 'pkgconfig(libxml-2.0)' 'pkgconfig(lilv-0)' 'pkgconfig(lrdf)' 'pkgconfig(lv2)' 'pkgconfig(ogg)' 'pkgconfig(pangoft2)' 'pkgconfig(pangomm-1.4)' 'pkgconfig(readline)' 'pkgconfig(rubberband)' 'pkgconfig(samplerate)' 'pkgconfig(serd-0)' 'pkgconfig(sigc++-2.0)' 'pkgconfig(sndfile)' 'pkgconfig(sord-0)' 'pkgconfig(sratom-0)' 'pkgconfig(taglib)' 'pkgconfig(vamp-hostsdk)' 'pkgconfig(vamp-sdk)' 'pkgconfig(x11)' 'pkgconfig(xext)' 'pkgconfig(xrandr)' 'pkgconfig(libpng)' 'pkgconfig(libudev)'
```

Then build Spite and install it for your user, under `~/.local` and apart from any Ardour. Run the script as yourself, not with sudo:

```sh
./install-spite.sh
~/.local/bin/spite9
```

The script also adds Spite to the application menu, and once `~/.local/bin` is on your PATH, `spite9` is enough. To update, run `git pull` and the script again. Spite keeps its settings in `~/.config/spite9`. At its first start it may warn about a limit on locked memory: that's the usual setup for realtime audio, and on Arch, installing `realtime-privileges` and joining the `realtime` group lifts it.

To run Spite straight from the clone without installing it, build it with the script's settings, so you can still install later:

```sh
python3 ./waf configure --prefix="$HOME/.local" --optimize --cxx17 --with-backends=jack,alsa,pulseaudio,dummy
python3 ./waf -j"$(nproc)"
./gtk2_ardour/ardev
```

After `./install-spite.sh`, `./gtk2_ardour/ardev` works on its own. To uninstall, run `python3 ./waf uninstall` in the clone, then `rm ~/.local/share/applications/spite9.desktop ~/.local/share/icons/hicolor/*/apps/spite9.png`. Your settings stay in `~/.config/spite9`.

### macOS (not yet tested with Spite)

Ardour's macOS guide, <https://ardour.org/building_osx_native.html>, builds the dependencies from source. Homebrew has them too, though Ardour doesn't support building with it. This route is untested; for JACK and control surfaces, add `jack libusb libwebsockets` to the `brew install` line:

```sh
xcode-select --install
brew install git pkgconf boost pangomm@2.46 libarchive lilv rubberband vamp-plugin-sdk aubio liblo taglib fftw jpeg-turbo
# clone Spite as above, then in the clone:
export PKG_CONFIG_PATH="$(brew --prefix libarchive)/lib/pkgconfig"
python3 ./waf configure --optimize --also-include="$(brew --prefix)/include" --also-libdir="$(brew --prefix)/lib"
python3 ./waf
./gtk2_ardour/ardev
```

Spite keeps its settings in `~/Library/Preferences/Spite9`.

### Windows (not yet tested with Spite)

Ardour's own Windows builds are cross-compiled on Linux with MinGW (`tools/x-win/compile.sh`), against dependencies that Ardour doesn't publish. Building natively with MSYS2 has been [reported to work for Ardour 9](https://discourse.ardour.org/t/building-ardour-on-windows-using-msys2/112412). That report used MSYS2's MINGW64 environment, which MSYS2 is retiring, so use UCRT64: install MSYS2 from <https://www.msys2.org>, open "MSYS2 UCRT64" from the Start menu, and install git and the libraries there:

```sh
pacman -Syu
pacman -S --needed git mingw-w64-ucrt-x86_64-{toolchain,boost,glib2,glibmm,libsndfile,curl,libarchive,liblo,taglib,vamp-plugin-sdk,rubberband,aubio,cairomm,pangomm,jack2,serd,sord,sratom,lilv,libwebsockets,libusb}
```

Then clone Spite in the same shell, and in the clone:

```sh
python3 ./waf configure --dist-target=mingw --prefix=/c/Spite --configdir=share --no-execstack --no-dr-mingw --optimize --cxx17
python3 ./waf
python3 ./waf install
/c/Spite/lib/spite9/ardour-*.exe
```

Start it from the same shell, because from File Explorer it can't find MSYS2's libraries. Spite keeps its settings in `%LOCALAPPDATA%\Spite9`.

Automated builds on macOS and Windows are next, so these two sections can be checked and fixed.

### plugdata with Detective Mode

plugdata builds on Linux, macOS and Windows with CMake 3.22 or newer, and a fresh clone of it builds on Linux as shown here. Its dependencies and options are in [plugdata's README](https://github.com/topemalheiro/plugdata/blob/debug-mode-visualizer/README.md). A quick build, which skips sfizz, Gem and ffmpeg:

```sh
git submodule update --init --recursive
cd plugdata
cmake -B build -DQUICK_BUILD=1
cmake --build build --parallel 8
cmake --install build --prefix ~/.local
```

The build puts the plugdata app and its plugins in `plugdata/Plugins`. On Linux, the last step copies the VST3 and LV2 plugins to `~/.vst3` and `~/.lv2`, where Spite finds them, and the app to `~/.local/bin/plugdata`; then use Scan for Plugins in Spite's Preferences, under Plugins. On macOS and Windows, `cmake --install` puts them in the system's plugin folders, which needs admin rights.

## Credit and licence

Spite is built on Ardour, created by Paul Davis and the Ardour community, and it keeps Ardour's full history and copyright notices. Ardour is licensed under the **GNU General Public License, version 2 or later** (see [LICENSE](LICENSE)), and so is Spite. Ardour's note on how its licence relates to plugins is kept in [PLUGIN-CLARIFICATION.txt](PLUGIN-CLARIFICATION.txt). All changes here are released under the same licence, and the source stays open.

Ardour's own documentation and build instructions are at <https://ardour.org/development.html>. The original `README` is kept as it was.

The Ardour source history lives under the `upstream` remote, so improvements from Ardour can still be pulled in:

```sh
git fetch upstream
```
