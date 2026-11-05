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

## Credit and licence

Spite is built on Ardour, created by Paul Davis and the Ardour community, and it keeps Ardour's full history and copyright notices. Ardour is licensed under the **GNU General Public License, version 2 or later** (see [LICENSE](LICENSE)), and so is Spite. Ardour's note on how its licence relates to plugins is kept in [PLUGIN-CLARIFICATION.txt](PLUGIN-CLARIFICATION.txt). All changes here are released under the same licence, and the source stays open.

Ardour's own documentation and build instructions are at <https://ardour.org/development.html>. The original `README` is kept as it was.

The Ardour source history lives under the `upstream` remote, so improvements from Ardour can still be pulled in:

```sh
git fetch upstream
```
