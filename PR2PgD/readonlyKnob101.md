# readonlyKnob101: read-only knobs drawn greyed out (plugdata PR 8)

**The links expect** `~/Projects/plugdata-prs/presentation-objects` to be checked out on `pr/gui-fixes` (it is). That branch holds all five of my GUI-fix PRs (4 to 8) together. `KnobObject.h` is identical there to this PR's own branch, `pr/fix-knob-readonly-greyed`, so checking that one out instead gives the same lines. The link into `Libraries/pd-else` opens ELSE at the version upstream plugdata uses.

## What this PR is, in plain words

**In short.** A read-only knob can't be turned, but it looked exactly like one that can. Now it's drawn greyed out. It's a new look, not a bug fix.

**What you run into without it.** ELSE's `[knob]` can be made read-only: the Inspector's "Read only", the `-readonly` flag, or a `readonly 1` message from the patch. A read-only knob can't be turned with the mouse, but it looks exactly like one that can, so you can't see why it doesn't respond. I checked it in upstream plugdata with `[knob -readonly]` next to a normal knob: they looked the same.

**Why.** Nothing in plugdata's knob drawing looked at read-only. In Pd itself, ELSE doesn't change the knob's look either ([knob.c:1497-1499](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/knob.c#L1497-L1499) only stores the flag). So this PR is a look choice for plugdata rather than a bug fix, and its description says so.

**What the PR changes.** A read-only knob is drawn greyed out:

- the value arc, the needle and the ticks lose their colour and fade;
- the arc behind them fades;
- the number label fades.

It redraws as soon as read-only changes, from the Inspector or from a `readonly` message, so a patch can visibly disable and enable a knob. One file, `Source/Objects/KnobObject.h`: 21 lines added, 5 removed. It's drawing code, the same on every platform.

**What it needs first.** Nothing. It's the last of my five, because it changes a look and may get discussion.

## How it came about

**Who ran into it, doing what.** Not you: an agent. On 9 November 2026 the converter agent was converting Flechtwerk, whose "LPG" switch disables some dials and a number box in Live by sending them `active 0`. In PgD4S nothing looked disabled, and Pd printed "nbx: no method for 'active'". The coordinator asked the objects agent for it "if they come cheap" (04:26).

**What caused it there.** ELSE's knob already uses `active` for something else (keyboard focus), so the converter turns a dial's `active 0/1` into `readonly 1/0`. That stopped the knob from turning, but it looked the same. So the coordinator asked (06:22) to "draw a readonly knob greyed out like Live's disabled dial, without changing pd-else".

**The first fix, in PgD4S.** Commit 603f40b0d (9 November): the value arc, the needle and the labels are greyed.

**From PgD4S to this PR.** The colours are the same. The PR also greys the ticks and the square knob's ring, which the first version missed. It leaves out two PgD4S parts: the tiny dial's needle, and a change that stops `readonly` messages from marking the patch as changed. Upstream marks the patch for every setting a message changes, so that's a separate question. I showed it in upstream with `[knob -readonly]` and `readonly` messages.

## Two classes, and two `readOnly`s

`KnobObject.h` holds two classes, and both have a `readOnly`:

- **`Knob`** ([KnobObject.h:18-30](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L18-L30)) is the dial that's drawn: arc, needle, ticks. Its `readOnly` (line 30) is a plain true/false that also stops the mouse from turning it. Blocks 1 to 7 are in this class.
- **`KnobObject`** is the plugdata object around it: Inspector settings, the link to Pd, the number label. Its `readOnly` is the Inspector's "Read only" setting, a JUCE `Value` that tells listeners when it changes. Blocks 8 to 10 are in this class.

When the setting changes, `KnobObject` passes it to the dial with `knob.setReadOnly(...)` (block 10).

## The code, in reading order

1. **[KnobObject.h:357-361](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L357-L361)** is `setReadOnly` on the dial. Line 360 is new: it asks for a redraw, so the new look shows at once. Before, the knob only changed how it reacts to the mouse, so there was nothing to redraw. See "Asking for a redraw".
   Python: `def set_read_only(self, value): self.read_only = value; self.update()`.
2. **[KnobObject.h:363-367](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L363-L367)** gives the colour the dial's foreground is drawn with: the usual colour, or when read-only, the same colour with no saturation (grey), 80% of its brightness and 55% of its opacity. See "Colours that return a changed copy".
   Python: `def shown_foreground(self): return self.fg.with_saturation(0).with_brightness_times(0.8).with_alpha_times(0.55) if self.read_only else self.fg`.
3. **[KnobObject.h:369-372](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L369-L372)** gives the colour of the arc behind the value: the usual one, or at 60% of its opacity when read-only.
   Python: `def shown_arc_colour(self): return self.arc.with_alpha_times(0.6) if self.read_only else self.arc`.
4. **[KnobObject.h:73](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L73)** draws the ticks (the dots around a knob with steps) in the colour from block 2 instead of the plain foreground colour.
   Python: `nvg.fill_color(self.shown_foreground())`.
5. **[KnobObject.h:276](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L276)** draws the full arc, the track the value moves along, in the colour from block 3.
   Python: `nvg.stroke_color(self.shown_arc_colour())`.
6. **[KnobObject.h:285](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L285)** draws the value arc, the coloured part up to the current value, in the colour from block 2.
   Python: `nvg.stroke_color(self.shown_foreground())`.
7. **[KnobObject.h:298](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L298)** draws the needle (plugdata calls it the wiper) in the colour from block 2.
   Python: `nvg.stroke_color(self.shown_foreground())`.
8. **[KnobObject.h:909](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L909)** is in `KnobObject`'s drawing. A square knob with "Show arc" off draws a thin ring instead of the arc, and that ring now uses the colour from block 3 too.
   Python: `nvg.stroke_color(self.knob.shown_arc_colour())`.
9. **[KnobObject.h:1017-1018](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L1017-L1018)** is in `updateLabel`, which sets up the number shown near the knob. When the knob is read-only, the label's text colour is drawn at half opacity, so it fades with the knob.
   Python: `if self.read_only: text_colour = text_colour.with_alpha_times(0.5)`.
10. **[KnobObject.h:1292-1297](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L1292-L1297)** runs when "Read only" changes, from the Inspector or from a `readonly` message ([KnobObject.h:791-795](../../plugdata-prs/presentation-objects/Source/Objects/KnobObject.h#L791-L795) sets it). It already stored the flag in Pd and passed it to the dial. Lines 1296-1297 are new: they rebuild the number label (block 9) and redraw the object, so the whole knob changes at once.
    Python: `self.update_label(); self.update()`.

Blocks 2 and 3 are the actual change: the greyed colours. Blocks 4 to 9 draw with them, and blocks 1 and 10 make the change show straight away.

## Colours that return a changed copy (blocks 2 and 3)

```cpp
        return readOnly ? fgColour.withSaturation(0.0f).withMultipliedBrightness(0.8f).withMultipliedAlpha(0.55f) : fgColour;
```

Each `with...` call gives a new colour and leaves the one it started from alone, so they chain:

- `withSaturation(0.0f)`: no colour, a grey of the same lightness;
- `withMultipliedBrightness(0.8f)`: 80% as bright;
- `withMultipliedAlpha(0.55f)`: 55% as opaque, so the background shows through.

`readOnly ? a : b` picks one of two values, like Python's `a if read_only else b`; [ovalButton101](ovalButton101.md#picking-one-of-two-values-blocks-2-and-3) explains it, with `0.55f`'s `f`.

Python works the same way with strings: `"Text".lower().strip()` makes a new string at each step.

## `const` after a function's name (blocks 2 and 3)

```cpp
    Colour getShownForeground() const
```

The `const` at the end promises the function only reads the dial and changes nothing in it; the compiler refuses to build it otherwise. That's why block 8 can call it on the dial from outside. Python has no such promise.

## Asking for a redraw (blocks 1 and 10)

`repaint()` doesn't draw anything itself. It marks the component as needing a redraw, and JUCE redraws it soon after, together with anything else waiting. So calling it twice (blocks 1 and 10 both do) costs nothing extra.

Python: like `widget.update()` in Qt, which schedules a paint event rather than painting right away.
