# ovalButton101: an "Oval outline" option for ELSE's [button] (plugdata PR 4, with an ELSE PR)

**The links expect** `~/Projects/plugdata-prs/presentation-objects` to be checked out on `pr/gui-fixes` (it is), with its `Libraries/pd-else` on `pr/button-oval-outline` (it is). `pr/gui-fixes` holds all five of my GUI-fix PRs (4 to 8) together. `ButtonObject.h` is identical there to this PR's own branch, `pr/fix-oval-button-unlit`. The link into `Libraries/nanovg` opens NanoVG at the version upstream plugdata uses.

## Which object this is

The object is **`[button]`**, from the **ELSE** library that plugdata ships with. Its full name is `else/button` (that is how a patch file stores it, for example `#X obj 244 166 else/button 50 50 ...` in [button-help.pd:30](../../plugdata-prs/presentation-objects/Libraries/pd-else/Documentation/Help-files/button-help.pd#L30)).

- **What it is:** a clickable GUI button you put in a patch. It can be square (the default) or oval, and it works in one of four modes, `latch` (the default), `toggle`, `bang` and `click` ([button-help.pd:161](../../plugdata-prs/presentation-objects/Libraries/pd-else/Documentation/Help-files/button-help.pd#L161)). The modes are settings of this one object, not objects of their own: searching for "latch" finds nothing, searching for "button" finds it.
- **How you make one in plugdata:** the "Button" entry in the Add Object menu ([AddObjectMenu.h:400](../../plugdata-prs/presentation-objects/Source/Dialogs/AddObjectMenu.h#L400)), or type `[button]` in an object box. Its "oval" setting is the `-oval` flag, or the Oval switch (No / Yes) in the Inspector.
- **What draws it in plugdata:** the class `ButtonObject` ([ButtonObject.h:8](../../plugdata-prs/presentation-objects/Source/Objects/ButtonObject.h#L8)). plugdata sees the object's name `"button"` and makes a `ButtonObject` for it ([ObjectBase.cpp:586-587](../../plugdata-prs/presentation-objects/Source/Objects/ObjectBase.cpp#L586-L587)).
- **Not these:** Pd's built-in `[bng]` (Bang, the round one) and `[tgl]` (Toggle) are different objects and aren't touched. Only ELSE's `[button]` with its oval option is.

**The parts of the button, by name:**
- **outline:** the box around the object, the part this PR is about;
- **background:** the button's colour when it's off;
- **foreground:** the fill when it's on (while you click it, in latch mode);
- **inner outline:** the ring inside, on buttons of 25 px or more.

## What this PR is, in plain words

**In short.** An oval `[button]` gets an Inspector switch, **Oval outline**. On (the default), the outline is round too; off, it keeps the square box. Clicking has nothing to do with it.

**What you run into without it.** In upstream plugdata, an oval button always sits in a square box; only its inner ring and its fill are round. A small one looks like a plain square. There's no way to choose.

**Why.** plugdata always drew the outline with the corner radius every object's box has. In Pd itself, ELSE draws an oval button as only its ellipse ([button.c:108-112](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L108-L112) hides the square). Some people like the square box, though, so the PR makes it a choice instead of forcing round.

**What the PRs change.** Two PRs, because the setting has to be saved in the patch, and only ELSE's `[button]` decides what it saves:
- **ELSE** (branch `pr/button-oval-outline` on github.com/topemalheiro/pd-else, for timothyschoen/pd-else, which plugdata uses): `[button]` gets an `ovaloutline` setting, 1 by default. It's saved as one more number at the end of the button's line in the patch, so older patches load unchanged, and it can be changed with an `ovaloutline 0` or `ovaloutline 1` message. Off, Pd's own drawing keeps the square too. 17 lines added, 3 changed, in `button.c`.
- **plugdata** (PR 4, branch `pr/fix-oval-button-unlit`): the Inspector switch, and the round outline when it's on. 21 lines added, 2 removed, in `ButtonObject.h`, plus moving plugdata's `Libraries/pd-else` to the ELSE commit.

It's drawing code and a saved setting, the same on every platform.

**What it needs first.** The ELSE PR: plugdata's PR points at its commit, so it should be merged first, or together.

## How it came about

**Who ran into it, doing what.** On 9 November 2026, while converting Max for Live devices to PgD4S, the coordinator listed "live.button round" among the leftovers: Live's `live.button` is round, and the converted Oscilloscope device showed square buttons. It asked the objects agent (06:22): "live.button is round in Live, but ELSE's button is square. If it's cheap, add a round look".

**What caused it there.** The objects agent found ELSE's `[button]` already has an oval option (07:02), and the converter switched it on. But in plugdata the oval button was still in its square box. It showed most on Oscilloscope's 15 px buttons, which are too small for the round inner ring.

**The first fix, in PgD4S.** Commit 67f14bdf7, "PgD4S: an oval button is round unlit too" (9 November, 07:11): always round, no option.

**From PgD4S to this PR.** The first version of PR 4 was that same forced change. On 14 November you looked at it and said the square box can be fine too: "make this shit a fucking option… call it oval outline". So it became the switch, with round as the default because that's how ELSE draws it in Pd.

## The code, in reading order

### ELSE: the setting itself (`button.c`)

1. **[button.c:24](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L24)** adds the setting to the button's data: 1 for a round outline, 0 to keep the square.
   Python: `self.oval_outline = 1` in the class's fields.
2. **[button.c:647](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L647)** sets it to 1 when a button is made, so every button starts with today's look.
   Python: `self.oval_outline = 1` in `__init__`.
3. **[button.c:707-709](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L707-L709)** reads it back when a patch loads: if there's one more number after the receive name, that's the setting. Older patches don't have it, so they keep 1.
   Python: `if args and isinstance(args[0], float): self.oval_outline = int(args[0] != 0)`.
4. **[button.c:299](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L299)** and **[button.c:307-308](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L307-L308)** save it: the format string gets one more `i` (a whole number), and the value goes last.
   Python: `line.append(int(self.oval_outline))` when writing the patch.
5. **[button.c:574-579](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L574-L579)** is what an `ovaloutline 0/1` message runs: it stores the setting and redraws the button in Pd's own window.
   Python: `def ovaloutline(self, f): self.oval_outline = int(f != 0); self.redraw_style()`.
6. **[button.c:858](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L858)** connects the message name `ovaloutline` to that function.
   Python: `handlers["ovaloutline"] = self.ovaloutline`.
7. **[button.c:133](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L133)** is Pd's own drawing (vanilla Pd, not plugdata): the square outline used to show only in edit mode; now it also shows for an oval button whose oval outline is off.
   Python: `if self.edit or (self.oval and not self.oval_outline): show_square_outline()`.

### plugdata: the switch and the drawing (`ButtonObject.h`)

8. **[ButtonObject.h:19](../../plugdata-prs/presentation-objects/Source/Objects/ButtonObject.h#L19)** adds the Inspector's value for the switch.
   Python: `self.oval_outline = Value()`.
9. **[ButtonObject.h:42](../../plugdata-prs/presentation-objects/Source/Objects/ButtonObject.h#L42)** puts "Oval outline" (No / Yes, default Yes) in the Inspector's Appearance section, under "Oval".
   Python: `params.add_bool("Oval outline", "Appearance", self.oval_outline, ["No", "Yes"], default=1)`.
10. **[ButtonObject.h:70](../../plugdata-prs/presentation-objects/Source/Objects/ButtonObject.h#L70)** reads the setting from Pd's button when the patch loads.
    Python: `self.oval_outline.value = button.x_oval_outline`.
11. **[ButtonObject.h:279-282](../../plugdata-prs/presentation-objects/Source/Objects/ButtonObject.h#L279-L282)** draws the outline: round (half the smaller side as corner radius) when the button is oval and the switch is on, otherwise the usual square box. See "Why half the smaller side makes it round".
    Python: `radius = min(w, h) * 0.5 if oval and oval_outline else corner_radius` and `draw_rounded_rect(..., radius)`.
12. **[ButtonObject.h:349-354](../../plugdata-prs/presentation-objects/Source/Objects/ButtonObject.h#L349-L354)** runs when you flip the switch in the Inspector: it writes the setting into Pd's button (so it's saved) and redraws.
    Python: `button.x_oval_outline = bool(self.oval_outline.value); self.repaint()`.
13. **[ButtonObject.h:394-400](../../plugdata-prs/presentation-objects/Source/Objects/ButtonObject.h#L394-L400)** follows an `ovaloutline` message from the patch: it updates the Inspector's switch and redraws.
    Python: `if msg == "ovaloutline": self.oval_outline.set_quietly(atoms[0]); self.repaint()`.

Block 11 is what you see change; blocks 3 and 4 are what make the setting stick in the patch. The rest connects them.

## Why half the smaller side makes it round (block 11)

A rectangle's corner radius is how big a circle each corner follows. When the radius is half the side, the two corners on that side meet in the middle, and the side becomes a half circle. A square whose radius is half its side is therefore a circle. A rectangle wider than tall, with a radius of half its height, is a pill: round ends and a straight top and bottom.

plugdata's own drawing function caps any larger radius at that same value ([nanovg.cpp:3044-3046](../../plugdata-prs/presentation-objects/Libraries/nanovg/src/nanovg.cpp#L3044-L3046)):

```cpp
    // If the radius is less than half of the shortest side, it will no longer be rounded
    // So force rounding here. Sorry not sorry.
    p.radius = std::min(radius, shortestSide * 0.5f);
```

That's why the inner ring and the lit fill were already round: [ButtonObject.h:284](../../plugdata-prs/presentation-objects/Source/Objects/ButtonObject.h#L284) gives them the whole width as their radius when the button is oval, and the drawing function caps it at half.

## Picking one of two values (block 11)

```cpp
        auto const roundOutline = getValue<bool>(oval) && getValue<bool>(ovalOutline);
        auto const outlineRadius = roundOutline ? std::min(b.getWidth(), b.getHeight()) * 0.5f : getPlugDataLook(*this).getObjectCornerRadius();
```

- `&&` is Python's `and`.
- `condition ? a : b` gives `a` when the condition is true, otherwise `b`. Python writes it `a if condition else b`.
- `auto const`: `auto` lets the compiler work out the variable's type, and `const` means it can't be changed after this line. Python: a plain assignment.
- `0.5f`: the `f` makes the number a `float`, C++'s smaller decimal type. Python has only one decimal type.
- `getValue<bool>(oval)` reads the Inspector's "Oval" switch as true or false. Python: `bool(self.oval.value)`.

## Saving a setting in a Pd patch (blocks 3 and 4)

A Pd patch is a text file, one line per object, and each object writes its own line. `[button]` writes all its settings in a fixed order:

```c
        x->x_snd_raw, x->x_rcv_raw,
        x->x_oval_outline);
```

That's why the setting had to go into ELSE: plugdata can draw anything, but only `[button]` itself decides what's written to its line. The new number goes at the end, after the receive name, because a `[button]` that doesn't know it (an older ELSE) stops reading there and ignores the rest. Python comparison: adding an optional last field to a CSV row, read only if it's there.

## Questions that came up

**Which object are we even talking about?** ELSE's `[button]`. See "Which object this is" at the top.

**What am I supposed to see when I click it?** Nothing new: clicking already worked, and the fill you see while clicking was always round. The PR is about the outline, the box around the button, whether you click or not.

**I searched "latch" and found nothing.** Latch isn't an object. It's one of `[button]`'s four modes, and the one a new `[button]` starts in. Search for "button".

**Where's the Inspector?** It shows when you select an object in edit mode, unless the sidebar is on a panel that uses the selection itself: Search or Help ([Sidebar.cpp:548-552](../../plugdata-prs/presentation-objects/Source/Sidebar/Sidebar.cpp#L548-L552)), and in your plugdata also Detective Mode. Switch the sidebar to another panel, such as the Console, or right-click the object and choose Properties ([Dialogs.cpp:784](../../plugdata-prs/presentation-objects/Source/Dialogs/Dialogs.cpp#L784)). Then it says "Inspector: button", with Oval and Oval outline under Appearance.

**How do I set it to latch?** It already is: latch is the default, and the Inspector has no Mode setting.

- Latch sends 1 when you press the mouse and 0 when you let go ([button.c:431-436](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L431-L436) and [312-318](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L312-L318)). It only does that in run mode, not while you edit the patch.
- The other modes come from a flag typed in the object box: `-tgl`, `-bng` or `-click` ([button.c:724-738](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/button.c#L724-L738)). There is no `-latch`, because latch is what you get without a flag.
- Or send it a message: a message box with `latch`, `tgl`, `bng` or `click`, connected to the button's inlet ([button-help.pd:94-107](../../plugdata-prs/presentation-objects/Libraries/pd-else/Documentation/Help-files/button-help.pd#L94-L107)).
