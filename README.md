# Spite

An open source DAW to finally unite sound engineers, music and audiovisual artists, and programmers as one.

## About

Spite is a clone of [Ardour](https://ardour.org/), not a fork meant to track it closely. I'm the maintainer, and I'll contribute to it extensively from now on. The changes are big enough that Spite will go its own way:

- **A new look.** A slicker, modern, flat art style inspired by plugdata, in place of Ardour's GTK2 interface.
- **Compatibility with the tools people already use.** Running Max for Live devices in Spite, so people only need a Max licence. Later, turning Max patches and devices into plugdata devices with Max-to-Pd transpilers and Cyclone (Pd objects cloned from Max, already included in plugdata), so anyone can make and share them without a licence.
- **Quality-of-life work.** Shortcuts and workflow improvements throughout.

## Roadmap

Most of it is about live performance on the Cue page, Spite's counterpart to Live's Session View:

- **More scenes, without the letters.** Ardour letters its cues A to P and fixes them at 16 per track. Spite takes the letters off and lets a set have as many scenes as it needs.
- **Deeper macros.** More macros, with deeper settings: one knob driving many parameters, each with its own range and curve.
- **Knobs that offset automation.** Recorded modulation (generative patches, LFOs and the modulators driving them) plays back from automation lines, and a control surface offsets it live without switching it off.
- **Sequencer control surfaces** that edit the MIDI notes and automation in Cue clips directly, with scripts that make other controllers, like the APC40 MKII, act like one.
- **Windows and device views.** Multiple windows per display, a MIDI editor for several clips and automation lines at once, and device views that aren't locked into a strip.
- **Colour coding.** A colour set per track or group of tracks, with hues of it for each device section.
- **Preset morphing.** Mappings and setups saved as presets and morphed between, on XY pads or pads with more corners.
- **Ready-to-play sets.** Cue page sets with the clips, devices, views and colours already laid out, for different kinds of music.

## Credit and licence

Spite is built on Ardour, created by Paul Davis and the Ardour community, and it keeps Ardour's full history and copyright notices. Ardour is licensed under the **GNU General Public License, version 2 or later** (see [COPYING](COPYING)), and so is Spite. All changes here are released under the same licence, and the source stays open.

Ardour's own documentation and build instructions are at <https://ardour.org/development.html>. The original `README` is kept as it was.

The Ardour source history lives under the `upstream` remote, so improvements from Ardour can still be pulled in:

```
git fetch upstream
```
