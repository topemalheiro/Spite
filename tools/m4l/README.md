# Max for Live devices in Spite

`m4l2pd.py` converts a Max for Live device (`.amxd`) into a plugdata patch, so it can run on a Spite track inside plugdata. It's a stdlib-only Python 3 script.

```sh
# convert one device into a folder
python3 tools/m4l/m4l2pd.py "My Device.amxd" out/

# convert and install: patches go to ~/.local/share/spite9/m4l/, and one LV2 preset
# per device goes to ~/.lv2/spite-m4l.lv2/
python3 tools/m4l/m4l2pd.py --install ~/Music/Devices/*.amxd
```

In Spite, add **plugdata-fx** (for audio effects) or **plugdata** (for instruments and MIDI effects) to a track. Then pick **M4L: <device name>** from the plugin's presets. Spite needs plugdata's LV2 plugins: build them from the `plugdata` submodule and copy or link `plugdata/Plugins/LV2/plugdata.lv2` and `plugdata-fx.lv2` into `~/.lv2`.

## What converts

- **Max and MSP objects.** plugdata includes cyclone, which provides Max's objects under their Max names, so most of them work as they are.
- **Subpatchers and bpatchers.** These become subpatches. Abstractions frozen inside the device become `.pd` files next to the patch.
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
- **Max-only helpers:**
  - `zmap`, and the simple forms of `if`, become `expr`;
  - `gswitch` and `gswitch2` become switching subpatches;
  - `pattr` passes values through.

Anything that can't be translated stays in the patch under its Max name, as a broken object with its connections, so it's easy to find and replace.

## What doesn't yet

- **The Live API** (`live.path`, `live.object`, `live.observer`, `live.remote~`). This needs a bridge to Spite.
- **JavaScript** (`js`, `v8`, `node.script`), **`gen~`** and **`poly~`**.
- **Devices encrypted by Ableton**, such as Live's own built-in devices, can't be converted, and the converter refuses them.

Converted devices remain their authors' work. Only convert devices you're allowed to use, and don't redistribute other people's converted devices unless their licence allows it.

Parts of the converter's design follow [max-pd-converter](https://github.com/tmhglnd/max-pd-converter) (MIT License, Copyright (c) 2020 Timo Hoogland). The notice is in `m4l2pd.py`.
