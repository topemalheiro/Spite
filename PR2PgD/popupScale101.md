# popupScale101: menus opened at the right size on Wayland (JUCE PR)

**The links expect** `~/Projects/plugdata-prs/popup-scale-juce` to be checked out on `pr/wayland-popup-scale` (one commit, `7eeea6b6f3`, on top of plugdata-team's `juce-master`).

## What this PR is, in plain words

**In short.** On KDE Wayland with a 120% screen, plugdata's dropdowns opened tiny.

- **Before:** the Inspector's "Log mode" menu came out at 60% of its size (50% on a 100% screen), with tiny text, a bit off its box.
- **After:** full size, with "Linear" right over the box.

Pictures: [before-after-120.png](../../../.local/share/claude-spite/prs/popup-scale/before-after-120.png), [before-after-100.png](../../../.local/share/claude-spite/prs/popup-scale/before-after-100.png).

**Why.**
- JUCE sizes a menu by dividing the scale of the window it's opened from (1.2) by the scale of the menu's own new window. That code is in [juce_PopupMenu.cpp:436-460](../../plugdata-prs/popup-scale-juce/modules/juce_gui_basics/menus/juce_PopupMenu.cpp#L436-L460), and the PR doesn't change it.
- It asks for the menu window's scale the moment the window is created. On Wayland that's too early: KWin only says "1.2" a moment later. Until then JUCE guessed the biggest whole-number scale of all screens, and KWin rounds 1.2 up to 2. So the menu got 1.2 ÷ 2 = 0.6.

**What the PR changes.** A menu now starts with the scale of the window it belongs to, until KWin sends its own. 3 files, 56 lines added, 1 changed. JUCE's Wayland files only: X11, Windows and macOS don't change.

**What it needs first.** Nothing. plugdata needs no change of its own, only its JUCE pointer moved to this commit once it's merged.

## How it came about

**Who ran into it, doing what.** On 14 November you set Inspector options on a `[knob]` (Show number, Transparent, Circular drag) in your own plugdata, the fork's `main` built on 13 November. Every dropdown opened tiny and shifted up-left. Your screenshots are in `~/.local/share/claude-spite/prs/popup-scale/`. PgD4S looked normal.

**What caused it there.** The fork's `main` took in upstream's JUCE 9.0.5 on 13 November, with its new Wayland window code. PgD4S still runs JUCE 8's window code, where a new window knows its scale at once. The Spite 3 agent traced it to the scale division above and handed it to Spite 2.

**From there to this PR.** Spite 2 reproduced it on upstream plugdata `develop`, in a nested KWin with one screen at 120% and one at 100%, like yours. The Wayland log showed the menu's size being sent before KWin's "1.2" arrived. It's upstream's bug, so the fix went straight into a JUCE PR. The same commit sits on top of your fork's `juce-master` as `ba6b32a868`.

## The code, in reading order

All in `modules/juce_gui_basics/native/`.

1. **[juce_Windowing_wayland_linux.cpp:1761-1769](../../plugdata-prs/popup-scale-juce/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L1761-L1769)** is a new helper, `getPopupOwner()`. If this window is a popup, it returns the window the popup belongs to; otherwise nothing (`nullptr`). JUCE already worked out the owner when the popup was created; this only reads it.
   Python: `def get_popup_owner(self): return self.popup_role.owner if self.popup_role and self.popup_role.owner else None`.
2. **[juce_Windowing_wayland_linux.cpp:1752-1757](../../plugdata-prs/popup-scale-juce/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L1752-L1757)** changes the whole-number guess for a window that isn't on a screen yet. A popup takes its owner's number. Any other window still takes the biggest of all screens, as before. This part is for compositors without fractional scaling; KDE needs block 3. See "Either-or in one line".
   Python: `scale_without_outputs = owner.integer_scale() if owner is not None else largest_output_scale()`.
3. **[juce_Windowing_wayland_linux.cpp:304-307](../../plugdata-prs/popup-scale-juce/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L304-L307)** runs once, when the popup's window is created: it copies the owner's scales into the popup, including the fractional 1.2. The comment says why it can't wait for KWin. `if (const auto* owner = ...)` makes `owner` and tests it in one go, like in [juceMaximize101.md](juceMaximize101.md), section "Is this window a Wayland one?".
   Python: `owner = self.get_popup_owner()` then `if owner: self.surface_scale.inherit_preferred_scales(owner.surface_scale)`.
4. **[juce_WaylandSurfaceScale_linux.h:67](../../plugdata-prs/popup-scale-juce/modules/juce_gui_basics/native/juce_WaylandSurfaceScale_linux.h#L67)** announces the copy function used in block 3, next to the other `set...` functions of the class that holds a window's scales.
   Python: a method stub, `def inherit_preferred_scales(self, other): ...`.
5. **[juce_WaylandSurfaceScale_linux.cpp:78-90](../../plugdata-prs/popup-scale-juce/modules/juce_gui_basics/native/juce_WaylandSurfaceScale_linux.cpp#L78-L90)** is that function. It copies the owner's two "preferred" scales (fractional and whole-number), but only the ones this window hasn't been told yet, so KWin's real answer always wins. It doesn't copy the owner's override: that's a scale a plugin host can force on its own window. See "Maybe a number: `std::optional`" and "Code handed to another function".
   Python: `if self.fractional is None: self.fractional = other.fractional` and the same for `buffer_scale`.
6. **[juce_WaylandSurfaceScale_linux.cpp:449-467](../../plugdata-prs/popup-scale-juce/modules/juce_gui_basics/native/juce_WaylandSurfaceScale_linux.cpp#L449-L467)** is a unit test. An owner at 1.2 (144 / 120) with an override of 1.5:
   - after the copy, the new window is at 1.2 and has no override;
   - KWin then says 1.5, and 1.5 wins;
   - a second copy doesn't undo it.
   Python: a `unittest` method with `self.assertEqual(state.scale(), 1.2)`.

Block 3 is the actual fix (with block 5, the copy it calls). Block 2 does the same for compositors without fractional scaling, block 1 finds the owner, block 4 only announces block 5, and block 6 tests it.

## Maybe a number: `std::optional` (block 5)

```cpp
        if (! preferredFractionalScale120.has_value())
            preferredFractionalScale120 = other.preferredFractionalScale120;
```

`preferredFractionalScale120` is a `std::optional<int>`: either a number or "nothing yet". `has_value()` asks which, and `!` is "not". So: if KWin hasn't told this window its fractional scale yet, take the owner's. The `120` in the name is because Wayland sends scales in 120ths: 144 means 1.2.
Python: `Optional[int]`, with `if self.fractional is None:`.

## Code handed to another function (block 5)

```cpp
    return updateScaleState ([&]
    {
        ...
    });
```

`[&] { ... }` is a lambda, a small function without a name. `updateScaleState` notes the scale, runs the lambda, notes the scale again, and reports whether it changed, so the window knows to redraw. The other `set...` functions do the same. `[&]` lets the lambda use the variables around it (here `other`) directly.
Python: passing a function, like `self.update_scale_state(lambda: ...)`, except a C++ lambda can hold several statements.

## Either-or in one line: `? :` (block 2)

```cpp
        const auto scaleWithoutOutputs = owner != nullptr ? owner->surfaceScale.getIntegerCompositorScale()
                                                          : windowSystem->getLargestOutputScale();
```

`condition ? a : b` gives `a` if the condition is true, otherwise `b`. `owner->surfaceScale` reads the owner's scales. They're private to the class, but C++ lets a class's code read the private parts of another object of the same class.
Python: `a if condition else b`.

## Questions that came up

None yet.
