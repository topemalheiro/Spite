# knobStyle101: a "Style" option for [knob], the knob or Live's tiny dial (plugdata PR 16, with ELSE PR 15)

**The links expect** these files:
- **The changed code:** read-only copies of the two changed files, exactly as they are on the PR branches, in `~/.local/share/claude-spite/prs/knob-style-review/`. `KnobObject.h` is plugdata `pr/knob-style` (4063c7d64), and `knob.c` is ELSE `pr/knob-style` (a56ee15d4).
- **The unchanged files** (the Add Object menu, the help patch, `Object.cpp`): `~/Projects/plugdata-prs/presentation-objects`, which stays checked out on `pr/gui-fixes` for the other notes.
- **The follow-up at the end:** your own plugdata, `~/Projects/plugdata`, on `main`.

## Which object this is

The object is **`[knob]`**, from the **ELSE** library that plugdata ships with. A patch file stores it as `else/knob` followed by its settings, for example [knob-help.pd:40](../../plugdata-prs/presentation-objects/Libraries/pd-else/Documentation/Help-files/knob-help.pd#L40).

- **How you make one in plugdata:** "Knob" in the Add Object menu ([AddObjectMenu.h:401](../../plugdata-prs/presentation-objects/Source/Dialogs/AddObjectMenu.h#L401)), or type `[knob]` in an object box. plugdata sees the name and makes a `KnobObject` for it ([ObjectBase.cpp:693-694](../../plugdata-prs/presentation-objects/Source/Objects/ObjectBase.cpp#L693-L694)).
- **What draws it:** two classes in the same file. `KnobObject` is the object: its Inspector options and its link to Pd. `Knob` is the dial inside it: it draws the arcs and the needle, and follows the mouse. readonlyKnob101 explains the pair: [Two classes, and two readOnlys](readonlyKnob101.md#two-classes-and-two-readonlys).

**The parts of the knob, by name** (with the Inspector options for each):
- **body:** the square, or the circle when Square is off ("Background", "Square", "Transparent");
- **arc:** the grey track around the dial ("Arc" colour, "Show arc");
- **value arc:** the coloured line from "Arc start" to the knob's value ("Foreground");
- **needle:** the line from the centre ("Foreground");
- **ring:** new in this PR. It's the tiny dial's thin circle with a gap, in the Arc colour.

## What this PR is, in plain words

**In short.** `[knob]` gets a **Style** choice in the Inspector: Knob, Tiny (left) or Tiny (right). The tiny ones look like Live's tiny dials: a thin ring with a gap, and a needle, with no coloured line and no body. ELSE's `[knob]` saves the choice, so it's two PRs, like the Oval outline.

**What you run into without it.** In upstream plugdata, a `[knob]` always draws as the full knob: body, grey arc, coloured value arc and needle. You can hide the arcs or the body, but you can't get Live's tiny dial. A converted Max for Live device shows its tiny dials as full knobs.

**Why.** plugdata has one drawing for the knob, and a patch has nowhere to keep a choice of look. ELSE's `[knob]` decides what's saved on its line in the patch, and it had no such setting.

**What the PRs change.** Two PRs, numbered 15 and 16 in order.md:
- **ELSE** (PR 15, branch `pr/knob-style`, for timothyschoen/pd-else, which plugdata uses): `[knob]` gets a `style` setting, 0, 1 or 2.
  - It's saved as one more number at the end of the knob's line, and only when it isn't 0.
  - A `style 2` message sets it.
  - Only plugdata draws it: in Pd itself the knob looks as before.
  - 12 lines added and 2 changed, in `knob.c`.
- **plugdata** (PR 16, branch `pr/knob-style`): the Inspector choice, the tiny dial's drawing, and a 45-degree turn of its range. 83 lines added and 1 changed, in `KnobObject.h`, plus moving plugdata's `Libraries/pd-else` to the ELSE commit (see hostStalls101's [A submodule pointer](hostStalls101.md#a-submodule-pointer-block-9)).

It's drawing code and a saved setting, the same on every platform.

**What it needs first.** PR 15, the ELSE PR: plugdata's PR points at its commit. Two more things concern whichever PR is merged second:
- PR 4 (the Oval outline) also moves plugdata's `Libraries/pd-else`. The second of 4 and 16 must point at an ELSE commit that has both changes.
- If PR 8 (read-only knobs greyed out) is in, the tiny dial needs the two-word follow-up at the end of this note.

## How it came about

**Who ran into it, doing what.** On 9 November 2026 you compared converted Max for Live devices with the originals. Flechtwerk's two "Mod" knobs and its FM knob are Live's tiny dials. The first try drew only a needle, and you said (11:49): "Why the fuck doesn't it have the circle? … All those I don't want any of those three."

On 14 November you opened your own plugdata, the fork's main, and the option wasn't there: "wasn't there style … option for plug data as it was before", then "just add the appearance setting".

**What caused it there.** Max's `live.dial` has four looks, set by its "appearance": Vertical (the usual knob), Tiny, Panel and Large. Flechtwerk uses Tiny. plugdata's `[knob]` had only the full knob, so the converter couldn't show the tiny ones.

**The first fix, in PgD4S.**
- Commit 1b4f6a117 (9 November) added three "needle" styles: a plain needle and two leaning ones. You rejected them.
- The objects agent replaced them in f7eb24012 (9 November, 12:51) with Knob, Tiny (left) and Tiny (right). They're measured from Max's own tiny dial, with a triangle at the initial value that you can click to go back to it.
- The converter writes Tiny (left) for every tiny dial: "Always Tiny (left)", your choice.

**From PgD4S to this PR.**
- **How it's saved:** PgD4S keeps the style in one of its own per-object attributes (an `#X attr` line), which only PgD4S's Pd understands. The PR saves it through ELSE's `[knob]` instead, the way the Oval outline is saved.
- **What it keeps:** the drawing, measurement for measurement, the 45-degree turn, and the missing body.
- **What it leaves out:** the triangle at the initial value, clicking it, and PgD4S's double-click back to the initial value. Those are behaviour, not looks.
- **Why it's upstream at all:** on 12 November you'd kept Live's looks in PgD4S only. On 14 November you made this one a PR: "so that's another PR".

## The code, in reading order

### ELSE: the setting itself (`knob.c`, PR 15)

1. **[knob.c:86](../../../.local/share/claude-spite/prs/knob-style-review/knob.c#L86)** adds the setting to the knob's data: 0 for the knob, 1 for Tiny (left), 2 for Tiny (right). The comment says only plugdata draws it.
   Python: `self.style = 0` among the class's fields.
2. **[knob.c:2328](../../../.local/share/claude-spite/prs/knob-style-review/knob.c#L2328)** sets it to 0 when a knob is made, in the line that already zeroed three other settings. So every knob starts as today's knob.
   Python: `self.flag = self.readonly = self.transparent = self.style = 0` in `__init__`.
3. **[knob.c:1514-1516](../../../.local/share/claude-spite/prs/knob-style-review/knob.c#L1514-L1516)** is what a `style 0`, `style 1` or `style 2` message runs. It keeps the number between 0 and 2 and stores it. It redraws nothing, because Pd's own drawing of the knob doesn't change. See "Keeping a number in range".
   Python: `def style(self, f): self.style = 0 if f < 0 else 2 if f > 2 else int(f)`.
4. **[knob.c:2822](../../../.local/share/claude-spite/prs/knob-style-review/knob.c#L2822)** ties the message name `style` to that function, with one number as its argument. See "A message name tied to a function".
   Python: `knob_class.methods["style"] = (knob_style, [float])`.
5. **[knob.c:1024-1025](../../../.local/share/claude-spite/prs/knob-style-review/knob.c#L1024-L1025)** saves it: after the knob's usual 32 values, one more number, but only when the style isn't 0. See "One more number, only when it's needed".
   Python: `if self.style: words.append(self.style)`.
6. **[knob.c:2366-2371](../../../.local/share/claude-spite/prs/knob-style-review/knob.c#L2366-L2371)** reads it back when a patch loads.
   - Line 2366 used to read the 32nd value, the default value, only when there were exactly 32. Now it's 32 or more, so a 33rd value doesn't hide it.
   - Lines 2370-2371 read the 33rd value, if there is one, through block 3's function, so it's kept in range too.

   Python: `default = args[31] if len(args) >= 32 else arc_start`, then `if len(args) >= 33: self.set_style(args[32])`.

### plugdata: the choice and the look (`KnobObject.h`, PR 16)

7. **[KnobObject.h:45-52](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L45-L52)** names the three looks and gives the drawing component (`Knob`) a `style`, starting as the normal knob. The comment describes the tiny dial. See "Named numbers: enum".
   Python: `class Style(IntEnum): NORMAL_KNOB = 0; TINY_LEFT = 1; TINY_RIGHT = 2`, then `self.style = Style.NORMAL_KNOB`.
8. **[KnobObject.h:453](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L453)** is the Inspector's value for the new option, like every other option of the knob.
   Python: `self.style = Value()`.
9. **[KnobObject.h:522](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L522)** puts it in the Inspector: "Style", in the Appearance section after Transparent, with three choices. The last `1` is the default, the first choice. See "The Inspector counts from 1".
   Python: `params.add_combo("Style", APPEARANCE, self.style, ["Knob", "Tiny (left)", "Tiny (right)"], default=1)`.
10. **[KnobObject.h:632](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L632)** is in `update()`, which reads all the knob's settings from Pd when the knob appears or changes. It copies ELSE's number into the Inspector, adding 1.
    Python: `self.style.value = knob.x_style + 1`.
11. **[KnobObject.h:663](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L663)**, also in `update()`, hands the style to the drawing component just before the angles are worked out, because block 18 needs it.
    Python: `self.knob.set_style(self.style.value - 1)`.
12. **[KnobObject.h:1402-1408](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L1402-L1408)** runs when you pick a style in the Inspector:
    - turns the choice back into ELSE's number, kept in range;
    - stores it in ELSE's knob, so the patch saves it (block 5);
    - hands it to the drawing component, works out the angles again (block 18), and redraws.

    `if (auto knb = ptr.get<t_fake_knob>())` holds Pd's lock for just the next line: see hostStalls101's [A lock held for one block](hostStalls101.md#a-lock-held-for-one-block-blocks-3-and-7).
    Python: `elif value is self.style: s = clamp(self.style.value, 1, 3) - 1; knob.x_style = s; self.knob.set_style(s); self.update_rotary_parameters(); self.repaint()`.
13. **[KnobObject.h:934-938](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L934-L938)** runs when a `style` message reaches the knob. Block 3 has already stored it in ELSE. This sets the Inspector's value, and that runs block 12, which redraws.
    Python: `case "style": self.style.value = clamp(int(atoms[0]), 0, 2) + 1`.
14. **[KnobObject.h:354-358](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L354-L358)**, `setStyle` in the drawing component, keeps the number between 0 and 2 and asks for a redraw.
    Python: `def set_style(self, s): self.style = clamp(s, 0, 2); self.repaint()`.
15. **[KnobObject.h:277-280](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L277-L280)** is in the drawing component's `render`, right after the needle's angle is worked out. A tiny style draws the tiny dial (block 16) and stops there, so none of the knob's own drawing runs.
    Python: `if self.style != Style.NORMAL_KNOB: self.render_tiny(nvg, start, end, angle); return`.
16. **[KnobObject.h:319-352](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L319-L352)**, `renderTiny`, draws the tiny dial:
    - **sizes** (lines 323-326): the ring's outer edge at 0.71 of half the knob's size, and its thickness 0.18 of that, at least 1.2 px. For a 70 px knob that's 24.85 px out and 4.47 px thick.
    - **the ring** (lines 328-342), in the Arc colour, only when Show arc is on. It's two arcs: from the start of the range to just before the needle, and from just after the needle to the end. The gap on each side is half the needle's thickness plus a little, 10.8 degrees for a 70 px knob.
    - **the needle** (lines 344-351), in the Foreground colour and as thick as the ring. It runs from the centre to the middle of the ring's line, with round ends.

    See "Angles, in radians" and "A small function inside a function".
    Python: drawing on a canvas: `ctx.arc(cx, cy, r, start, angle - gap)`, `ctx.arc(cx, cy, r, angle + gap, end)`, then `ctx.line(centre, tip)`.
17. **[KnobObject.h:950-954](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L950-L954)** is in the object's own `render`, which draws the knob's body (the square or the circle) before the dial. A tiny dial has no body, as in Live, so it draws only the dial.
    Python: `if self.knob.style != Style.NORMAL_KNOB: self.knob.render(nvg); return`.
18. **[KnobObject.h:1218-1221](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L1218-L1221)** turns the tiny dial's range by 45 degrees. Tiny (left) turns it left, so the range's middle points to the top left and the gap sits at the bottom right. Tiny (right) turns it right. The normal knob doesn't turn. See "Angles, in radians".
    Python: `turn = -pi / 4 if style == TINY_LEFT else pi / 4 if style == TINY_RIGHT else 0`, then `start += turn; end += turn`.
19. **[KnobObject.h:555](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L555)**, `isTransparent()`, now also says yes for a tiny dial. In edit mode, plugdata draws a faint box behind objects that say yes ([Object.cpp:1214-1217](../../plugdata-prs/presentation-objects/Source/Object.cpp#L1214-L1217)), so you can see where a tiny dial's box is.
    Python: `return not self.square.value or self.knob.style != Style.NORMAL_KNOB`.

Block 16 is the new look. Block 12 makes it a choice, and blocks 3, 5 and 6 make that choice stick in the patch. The rest connect them.

## In your own plugdata only: the tiny dial greys out too

PR 8 draws a read-only knob in greyed colours, through two functions, `getShownArcColour()` and `getShownForeground()`. The tiny dial is new in PR 16, so it uses the plain colours. Where both PRs are in, it has to use the greyed ones too. Your plugdata's main has both, so it has this two-word follow-up (commit 4aa152be0):

1. **[KnobObject.h:332](../../plugdata/Source/Objects/KnobObject.h#L332)** draws the ring with `getShownArcColour()` instead of `arcColour`.
   Python: `ring_colour = self.shown_arc_colour()`.
2. **[KnobObject.h:350](../../plugdata/Source/Objects/KnobObject.h#L350)** draws the needle with `getShownForeground()` instead of `fgColour`.
   Python: `needle_colour = self.shown_foreground()`.

Upstream, it goes into whichever of PR 8 and PR 16 is merged second.

## Named numbers: enum (block 7)

```cpp
    enum Style {
        NormalKnob = 0,
        TinyLeft,
        TinyRight
    };
    int style = NormalKnob;
```

An `enum` gives names to whole numbers. `NormalKnob` is 0, and each next name is one more: `TinyLeft` is 1 and `TinyRight` is 2. They're the same numbers ELSE saves (block 1), so the code can say `knob.style == Knob::TinyLeft` instead of `knob.style == 1`. `Knob::` says the names belong to the `Knob` class. `int style` is a plain whole number, so it can hold any of them.

Python: `class Style(IntEnum): NORMAL_KNOB = 0; TINY_LEFT = 1; TINY_RIGHT = 2`.

## A message name tied to a function (blocks 3 and 4)

```c
static void knob_style(t_knob *x, t_floatarg f){
    x->x_style = f < 0 ? 0 : f > 2 ? 2 : (int)f; // plugdata draws it; here the knob looks the same
}
```

```c
    class_addmethod(knob_class, (t_method)knob_style, gensym("style"), A_FLOAT, 0);
```

- **Looking up the message:** when a message reaches a Pd object, Pd looks up its first word in the object's class. That's a table, filled once when ELSE loads.
- **Adding a row to it:** `class_addmethod` adds a row with three things:
  - the word `style` (`gensym` turns the text into Pd's symbol for it);
  - the function to run;
  - what arguments it takes: `A_FLOAT` is one number, and `0` ends the list.
- **Calling the function:** Pd then calls `knob_style` with the knob and the number. The knob comes as `x`, an address of its data: see saveCanvas101's [A struct, and what the stars mean](saveCanvas101.md#a-struct-and-what-the-stars-mean-block-1). The number comes as `f`.
- **`(t_method)`:** it tells the compiler to accept the function as a general "method", whatever its own arguments are.

Python: `knob_class.methods["style"] = (knob_style, [float])`, and later `func, _ = knob_class.methods[msg[0]]; func(knob, *msg[1:])`.

## Keeping a number in range (blocks 3, 12, 13 and 14)

```c
    x->x_style = f < 0 ? 0 : f > 2 ? 2 : (int)f; // plugdata draws it; here the knob looks the same
```

```cpp
        style = std::clamp(newStyle, static_cast<int>(NormalKnob), static_cast<int>(TinyRight));
```

Both keep the style between 0 and 2, so a `style 7` or `style -1` message can't make a style that doesn't exist.
- `a ? b : c` is Python's `b if a else c` (see ovalButton101's [Picking one of two values](ovalButton101.md#picking-one-of-two-values-block-11)). Two in a row read left to right: below 0 gives 0, otherwise above 2 gives 2, otherwise the number itself.
- `(int)f` and `static_cast<int>(...)` make a whole number by dropping what's after the point, like Python's `int()`: `(int)1.7` is 1.
- `std::clamp(v, low, high)` is the C++ library's version: `low` if `v` is below it, `high` if above, otherwise `v`.

Python: `max(0, min(2, int(f)))`.

## The Inspector counts from 1 (blocks 9 to 13)

A choice in plugdata's Inspector stores which item is picked, counting from 1: Knob is 1, Tiny (left) 2, Tiny (right) 3. ELSE stores 0, 1 and 2. So the code adds 1 going into the Inspector (blocks 10 and 13) and takes 1 away coming out (blocks 11 and 12). The knob's other choices already do the same, for example "Circular drag":

```cpp
            circular = knb->x_circular + 1;
```

That's [KnobObject.h:625](../../../.local/share/claude-spite/prs/knob-style-review/KnobObject.h#L625), unchanged.

Python: `inspector_value = else_value + 1` and `else_value = inspector_value - 1`.

## One more number, only when it's needed (blocks 5 and 6)

ELSE saves a knob as one line in the patch file, with its 32 settings in a fixed order ([knob-help.pd:40](../../plugdata-prs/presentation-objects/Libraries/pd-else/Documentation/Help-files/knob-help.pd#L40) is one). The basics are in ovalButton101's [Saving a setting in a Pd patch](ovalButton101.md#saving-a-setting-in-a-pd-patch-blocks-3-and-4): a new setting goes at the end. One detail changes how it's added here. Before the PR, a knob read its 32nd value, the default value, only if the line had exactly 32. The first of these lines was `if(ac == 32)`:

```c
            if(ac >= 32)
                def = atom_getfloatarg(31, ac, av); // 32: f default value
            else
                def = arcstart;
```

With that old check, a 33rd number makes an older ELSE (or an older plugdata) skip the default value and use the arc start instead. So the style is written only when it isn't 0:

```c
    if(x->x_style) // 33: i style, only when set, so other knobs save as before
        binbuf_addv(b, "i", x->x_style);
```

- **Other knobs:** every knob that stays a knob saves exactly the line it saved before, so nothing changes for it anywhere.
- **Tiny dials:** only they get the 33rd number. Opened in an older version, one shows as a plain knob whose default value is its arc start.

How the lines read:
- `binbuf_addv(b, "i", ...)` adds to the line being written, and `"i"` says one whole number follows. See hostStalls101's [binbuf: Pd's list of words](hostStalls101.md#binbuf-pds-list-of-words-blocks-3-to-5).
- `ac` is how many values the line had.
- `atom_getfloatarg(31, ac, av)` reads value 32, counting from 0.

Python: `if self.style: words.append(int(self.style))`. When loading: `default = args[31] if len(args) >= 32 else arc_start`.

## Angles, in radians (blocks 15, 16 and 18)

```cpp
        auto const turn = knob.style == Knob::TinyLeft ? -MathConstants<float>::pi * 0.25f : knob.style == Knob::TinyRight ? MathConstants<float>::pi * 0.25f : 0.0f;
```

The drawing measures angles in radians: a full turn is 2π (about 6.28), so 45 degrees is π/4, `pi * 0.25f`. `MathConstants<float>::pi` is JUCE's π.

- **The range, as ELSE keeps it:** two angles in degrees, measured clockwise from the top. With an Angular range of 270 and no offset, they're -135 and 135 ([knob.c:2708-2709](../../../.local/share/claude-spite/prs/knob-style-review/knob.c#L2708-L2709)): from half past seven, over the top, to half past four, with the gap at the bottom.
- **Turned for the tiny dial:**
  - Tiny (left) gives -180 to 90: from straight down, round the left and the top, to the right. The gap fills the bottom-right quarter.
  - Tiny (right) gives -90 to 180, the mirror image.
- **Where plugdata converts them:** `updateRotaryParameters` turns them into radians, just above block 18. NanoVG, which draws, measures from the right (three o'clock), so `render` takes a quarter turn off before drawing.
- **The gap beside the needle (block 16)** is an angle too. A length along the ring divided by the ring's radius gives radians.

Python: `math.pi / 4`, or `math.radians(45)`.

## A small function inside a function (block 16)

```cpp
            auto const strokeArc = [nvg, centre, ringRadius](float const from, float const to) {
                if (to - from <= 0.001f)
                    return;
                nanovg::nvgBeginPath(nvg);
                nanovg::nvgArc(nvg, centre.x, centre.y, ringRadius, from, to, NVG_HOLE);
                nanovg::nvgStroke(nvg);
            };
            strokeArc(startAngle, std::min(angle - gap, endAngle));
            strokeArc(std::max(angle + gap, startAngle), endAngle);
```

`strokeArc` is a lambda: a function made on the spot and kept in a variable, so the ring's two pieces are drawn by the same three lines.
- **The square brackets** say which of the surrounding variables it takes along: `nvg` (where it draws), `centre` and `ringRadius`. It takes copies, made when the lambda is made.
- **The round brackets** are its own arguments: the start and end angle.
- **The first two lines** skip an arc with no length, which happens when the needle is at one end of the range.
- **`std::min` and `std::max`** keep each piece inside the range.

Python: a nested `def stroke_arc(start, end): ...` that uses `nvg`, `centre` and `ring_radius` from the function around it.

## Questions that came up

**Is this the "appearance" setting from Max?** Yes. Max calls it `live.dial`'s Appearance: Vertical (the usual knob), Tiny, Panel and Large. This adds Tiny, both ways round, as "Style", because the Inspector section it's in is already called Appearance.

**Can I show the coloured line or not, and choose the side it starts from?** The tiny styles have no coloured line, just the ring and the needle. Tiny (left) and Tiny (right) choose which side the gap is on. On the normal knob, the coloured line runs from "Arc start" (a value, in General) to the knob's value. "Show arc" off hides it together with the grey arc.

**Foreground changes the needle colour too?** Yes, in both styles: Foreground colours the needle, and on the normal knob the coloured line as well. The ring and the grey arc use "Arc". Separate colours for the needle and the line aren't in this PR.
