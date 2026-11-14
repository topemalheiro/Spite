# juceMaximize101: ask a Wayland window "maximized or tiled?" (JUCE PR 3)

**The links expect** `~/Projects/plugdata-prs/maximize-juce` to be checked out on `pr/linux-maximize` (one commit, `32928d231c`, on top of plugdata-team's `juce-master`).

## What this PR is, in plain words

**What you run into without it.** Nothing in JUCE itself breaks. The gap shows in plugdata on a Linux Wayland desktop (KDE, for example): half-tile the plugdata window (Meta+Left or Meta+Right) and it keeps its 18 pixel empty margin, its rounded corners and its fake shadow, even though the compositor now fills the screen half edge to edge. I reproduced it in a nested KWin on two virtual screens: on upstream `develop` the margin stays at 18 after a half-tile, on both screens.

**Why.** plugdata draws its own shadow around its window, and has to drop it when the compositor places the window edge to edge. It can only drop it if it knows the window is maximized or tiled. When the compositor changes a window, it sends the app a "configure" message listing the window's states (maximized, tiled left, tiled right...). JUCE already works out "maximized or tiled on at least one edge" from that message (`info.maximisedOrTiled`) and uses it inside, but the public function it offers apps, `isWaylandWindowMaximised()`, only says "strictly maximized". A half-tiled window looks like a floating one to plugdata.

**What the PR changes.** It keeps that value on the window and adds one public function, `isWaylandWindowMaximisedOrTiled()`, next to the existing one. 25 lines added in 2 files, nothing removed, nothing changes for code that doesn't call the new function. Linux with Wayland only: the files are the Wayland ones, X11, Windows and macOS don't see it. Both files use Windows-style line endings (CRLF) and the new lines do too.

**What it needs first.** Nothing. PR 12 (plugdata, Linux maximize) needs this one.

## The code, in reading order

Everything is one new yes/no value, `maximisedOrTiled`, copied from how JUCE already keeps `maximised`: declared, filled from each configure message, cleared when the window goes away, read by a small getter, and offered to apps by a function.

1. **[juce_WaylandWindowState_linux.h:57-64](../../plugdata-prs/maximize-juce/modules/juce_gui_basics/native/juce_WaylandWindowState_linux.h#L57-L64)** announces the new public function and explains it in a comment (lines 57-63; the announcement itself is line 64). It takes the window and answers yes or no. This is only the first line of the function, ending in `;`; its code is block 6 (see "One function, written twice").
   Python: a `def is_wayland_window_maximised_or_tiled(peer) -> bool: ...` stub with a docstring, no body.
2. **[juce_Windowing_wayland_linux.cpp:2460](../../plugdata-prs/maximize-juce/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L2460)** adds the yes/no slot to the Wayland window object, next to `maximised`, starting as `false`.
   Python: `self.maximised_or_tiled = False` in `__init__`.
3. **[juce_Windowing_wayland_linux.cpp:1901](../../plugdata-prs/maximize-juce/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L1901)** fills the slot every time the compositor sends a configure message: `info` is that message already read, and it already has `maximisedOrTiled` in it. So the new value changes at the same moment as the window's size.
   Python: `self.maximised_or_tiled = info.maximised_or_tiled`.
4. **[juce_Windowing_wayland_linux.cpp:1387](../../plugdata-prs/maximize-juce/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L1387)** empties the slot when the window is unmapped (hidden or destroyed). The compositor forgets all window state then, and the line above it already does the same for `maximised`.
   Python: `self.maximised_or_tiled = False` in the method that hides the window.
5. **[juce_Windowing_wayland_linux.cpp:590-593](../../plugdata-prs/maximize-juce/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L590-L593)** a getter: a tiny method on the window that hands the slot's value out. The slot (block 2) is private to the class, so the outside world needs a method to read it. `const` after the name means the method only reads, it changes nothing.
   Python: `def is_maximised_or_tiled(self): return self.maximised_or_tiled`, or a read-only `@property`.
6. **[juce_Windowing_wayland_linux.cpp:2591-2598](../../plugdata-prs/maximize-juce/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L2591-L2598)** is the function block 1 announced, now with its code. It is given a window of any kind (`ComponentPeer`). If that window is a Wayland one it asks it (block 5); if it isn't (an X11 window, say) it answers `false`. See "Is this window a Wayland one?" below.
   Python: `def is_wayland_window_maximised_or_tiled(peer): return peer.is_maximised_or_tiled() if isinstance(peer, WaylandPeer) else False`.

Block 3 is the one that makes the value true or false; blocks 1 and 6 are what apps call; blocks 2, 4 and 5 keep and read it. Nothing here is new logic: `info.maximisedOrTiled` was already computed in [juce_WaylandToplevel_linux.cpp:364-370](../../plugdata-prs/maximize-juce/modules/juce_gui_basics/native/juce_WaylandToplevel_linux.cpp#L364-L370) (maximized, or tiled left, right, top or bottom), and this PR only keeps it and makes it reachable.

## One function, written twice (blocks 1 and 6)

In C and C++, a function other files use is written in two places. The declaration is the first line only, ending in `;`, in the header (`.h`) that other files include. Block 1 is that:

```cpp
JUCE_API bool isWaylandWindowMaximisedOrTiled (const ComponentPeer&);
```

The definition is the same first line again, then the code between `{` and `}`. Block 6 is that:

```cpp
bool isWaylandWindowMaximisedOrTiled (const ComponentPeer& peer)
{
    if (auto* waylandPeer = dynamic_cast<const WaylandComponentPeer*> (&peer))
        return waylandPeer->isMaximisedOrTiled();

    return false;
}
```

`JUCE_API` marks the function as one JUCE offers to programs using it. `bool` is yes/no. `const ComponentPeer&` is "a window, which this function will not change"; the `&` means it gets the window itself, not a copy. The same thing is explained with another function in [pdHook101.md](pdHook101.md), section "One function, written twice". Python has no split like this: `def` is the declaration and the body at once.

## Is this window a Wayland one? (block 6)

```cpp
if (auto* waylandPeer = dynamic_cast<const WaylandComponentPeer*> (&peer))
    return waylandPeer->isMaximisedOrTiled();
return false;
```

The function gets a general "window" (`ComponentPeer`), but only some are Wayland windows (others are X11, Windows, macOS). `dynamic_cast<const WaylandComponentPeer*> (&peer)` asks: "is this one really a Wayland window? If yes, give me it as one; if not, give me `nullptr` (nothing)". `&peer` is the address of the window, because `dynamic_cast` works on pointers (the `*` in `auto*`). The `if (auto* x = ...)` makes `waylandPeer` and tests it in one go: inside the `if` it's a real Wayland window, and `->` calls a method on it. When the answer is nothing, the `if` is false and the function reaches `return false`.

Python: `if isinstance(peer, WaylandComponentPeer): return peer.is_maximised_or_tiled()` then `return False`.

## Questions that came up

None yet.
