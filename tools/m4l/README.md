# Max for Live devices in Spite

`m4l2pd.py` converts a Max for Live device (`.amxd`) into a plugdata patch, so it can run on a Spite track inside plugdata. It's a stdlib-only Python 3 script.

```sh
# convert one device into a folder
python3 tools/m4l/m4l2pd.py "My Device.amxd" out/

# convert and install: patches go to ~/.local/share/spite9/m4l/, and one LV2 preset
# per device goes to ~/.lv2/spite-m4l.lv2/
python3 tools/m4l/m4l2pd.py --install ~/Music/Devices/*.amxd
```

In Spite, add **PgD4S-fx** (for audio effects) or **PgD4S** (for instruments and MIDI effects) to a track. PgD4S is plugdata for Spite, built from the `PgD4S` submodule. Then pick **M4L: <device name>** from the plugin's presets. On the Cue page, the Device view (Tab) shows the device's face, as Live does, and double-clicking its title bar opens the patch. To build the plugins, link `PgD4S/Plugins/LV2/PgD4S.lv2` and `PgD4S-fx.lv2` into `~/.lv2`.

## What converts

The converter uses PgD4S's own objects and options for these devices: knobs that draw their name and Live's tiny needles, Live's plain number boxes (no arrow, decimals, unit), `spite.tab` for `live.tab`, real `pcontrol` and `thispatcher` objects, and subpatches that open as a window on their presentation. For a PgD4S build older than those, `--objects old` writes stock plugdata objects and stand-in subpatches instead.

- **The face.** Max's presentation layout becomes PgD4S's own presentation layout (`#X presentation` lines): every object keeps its patching position and also gets its presentation one. The patch window looks like Max's patching view; the Device view, PgD4S's plugin mode and its presentation mode show the face. Bpatchers become graph-on-parent windows onto their own presentation layout.
- **The patch's layout.** plugdata draws box text larger than Max, so the patching view is Max's layout widened to fit it: every box keeps its Max width in that scale, and rows move apart only where plugdata's boxes (or the helpers a translation adds) would overlap when Max's don't. Every cord stays.
- **Tidy cords.** Cords routed with midpoints in Max keep their route, as PgD4S's segmented connections.
- **Live's controls, in Live's colours:** `live.dial` becomes a knob with its name, `live.numbox` a number box with its unit, `live.menu` a drop-down menu, `live.tab` a grid of cells (with the device's own pictures), `live.text` a text button.

- **Max and MSP objects.** plugdata includes cyclone, which provides Max's objects under their Max names, so most of them work as they are.
- **Subpatchers and bpatchers.** These become subpatches, with their own presentation layout when they have one. Abstractions frozen inside the device become `.pd` files next to the patch.
- **MIDI into `poly~`.** `midiparse`'s eighth outlet (each MIDI message as a `midievent`) feeds a `poly~` voice's `polymidiin`, as in Max.
- **Max for Live I/O.**
  - `plugin~` and `plugout~` become `adc~` and `dac~`.
  - Local `---names` become `$0-names`, one set per device instance.
- **Max for Live controls:**
  - `live.dial` becomes a knob, with its range and initial value;
  - `live.numbox` becomes a number box;
  - `live.toggle` and `live.text` become a toggle or a button;
  - `live.menu` and `live.tab` become a radio selector that also sends the item's name;
  - `live.gain~` becomes a dB gain stage.
- **Tempo-synced times.** `metro 16n`, `qmetro` and `phasor~ 4n` get fixed times at 120 BPM, for now.
- **Window control:** `pcontrol` (open and close a subpatcher) and `thispatcher` (scroll a bpatcher, size or show a canvas) become Pd canvas messages.
- **Known compiled externals,** rebuilt in Pd: trnr.Flechtwerk's `trnr.core.flechtwerk~` (a Mutable Instruments Plaits port) runs on ELSE's `[plaits~]`.
- **Max-only helpers:**
  - `zmap`, and the simple forms of `if`, become `expr`;
  - `gswitch` and `gswitch2` become switching subpatches;
  - `pattr` passes values through, and to the object it's bound to;
  - a message box's right inlet (set without sending) and `sel`'s per-value inlets keep working through small helpers, as Pd has neither;
  - `scope~` becomes ELSE's `scope~`, with Max's size, colours and trigger.

Anything that can't be translated stays in the patch under its Max name, as a broken object with its connections, so it's easy to find and replace.

## What doesn't yet

- **The Live API** (`live.path`, `live.object`, `live.observer`, `live.remote~`). This needs a bridge to Spite.
- **JavaScript** (`js`, `v8`, `node.script`), **`gen~`**, multichannel (`mc.*`) objects, and `poly~` with several voices (one voice works).
- **Live's transport** (`transport`, `live.step`, tempo-locked `phasor~`): sequencers such as Snake Mod Super show their face but don't step yet.
- **Devices encrypted by Ableton**, such as Live's own built-in devices, can't be converted, and the converter refuses them.

Converted devices remain their authors' work. Only convert devices you're allowed to use, and don't redistribute other people's converted devices unless their licence allows it.

Parts of the converter's design follow [max-pd-converter](https://github.com/tmhglnd/max-pd-converter) (MIT License, Copyright (c) 2020 Timo Hoogland). The notice is in `m4l2pd.py`.
