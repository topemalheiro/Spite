# Spite

An open source DAW to finally unite sound engineers, music and audiovisual artists, and programmers as one.

## About

Spite is a clone of [Ardour](https://ardour.org/), not a fork meant to track it closely. I'm the maintainer, and I'll contribute to it extensively from now on. The changes are big enough that Spite will go its own way:

- **A new look.** A simple, flat art style inspired by PlugData, in place of Ardour's GTK2 interface.
- **Compatibility with the tools people already use.** Opening Ableton Live sets directly (potentially, and Pro Tools too later perhaps), and running Max for Live devices as PlugData devices inside Spite.
- **Quality-of-life work.** Shortcuts and workflow improvements throughout.

## Credit and licence

Spite is built on Ardour, created by Paul Davis and the Ardour community, and it keeps Ardour's full history and copyright notices. Ardour is licensed under the **GNU General Public License, version 2 or later** (see [COPYING](COPYING)), and so is Spite. All changes here are released under the same licence, and the source stays open.

Ardour's own documentation and build instructions are at <https://ardour.org/development.html>. The original `README` is kept as it was.

The Ardour source history lives under the `upstream` remote, so improvements from Ardour can still be pulled in:

```
git fetch upstream
```
