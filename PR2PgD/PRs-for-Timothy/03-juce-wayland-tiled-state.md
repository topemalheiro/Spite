# Wayland: Add isWaylandWindowMaximisedOrTiled()

[← Overview](README.md) · **PR:** [plugdata-team/JUCE#8](https://github.com/plugdata-team/JUCE/pull/8) · JUCE `juce-master` ← `topemalheiro:pr/linux-maximize` · 1 commit, +25 in 2 files · Linux with Wayland only · needs nothing first · [plugdata-team/plugdata#2433](https://github.com/plugdata-team/plugdata/pull/2433) needs this one ([12-linux-maximize.md](12-linux-maximize.md))

## What it does

**What you run into without it.** Nothing in JUCE itself breaks. The gap shows in plugdata on a Wayland desktop: half-tile the window (Meta+Left or Meta+Right on KDE) and it keeps its 18 px empty margin, its rounded corners and its shadow, even though the compositor now places it edge to edge. I reproduced it in a nested KWin with two outputs: on `develop` the margin stays at 18 after a half-tile, on both outputs.

**Why.** plugdata draws its own shadow around its window, and has to drop it when the compositor places the window edge to edge. It can only drop it if it knows the window is maximised or tiled. JUCE already works out "maximised or tiled on at least one edge" from each configure (`info.maximisedOrTiled`) and uses it internally, but the public function, `isWaylandWindowMaximised()`, only says "strictly maximised". A half-tiled window looks like a floating one to plugdata.

**What changes.** The peer keeps that value, and one public function, `isWaylandWindowMaximisedOrTiled()`, is added next to the existing one. Nothing changes for code that doesn't call the new function. Both files are CRLF and the new lines are too.

## The code, in reading order

Everything is one new bool, `maximisedOrTiled`, handled exactly as the peer already handles `maximised`: declared, set from each configure, cleared when the toplevel is unmapped, read by a small getter, and exposed by a free function.

1. **[juce_WaylandWindowState_linux.h:57-64](https://github.com/topemalheiro/JUCE/blob/32928d231cc46b2133c2dfe72d5cb3e9ccfd5ac7/modules/juce_gui_basics/native/juce_WaylandWindowState_linux.h#L57-L64)** declares the new public function, with its doc comment (lines 57-63; the declaration is line 64).
2. **[juce_Windowing_wayland_linux.cpp:2460](https://github.com/topemalheiro/JUCE/blob/32928d231cc46b2133c2dfe72d5cb3e9ccfd5ac7/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L2460)** adds the member to the Wayland peer, next to `maximised`, starting as `false`.
3. **[juce_Windowing_wayland_linux.cpp:1901](https://github.com/topemalheiro/JUCE/blob/32928d231cc46b2133c2dfe72d5cb3e9ccfd5ac7/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L1901)** sets it every time the compositor sends a configure, from `info.maximisedOrTiled`. So the new value changes in the same configure as the window's size.
4. **[juce_Windowing_wayland_linux.cpp:1387](https://github.com/topemalheiro/JUCE/blob/32928d231cc46b2133c2dfe72d5cb3e9ccfd5ac7/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L1387)** clears it when the toplevel is unmapped. The compositor discards all toplevel state then, and the line above it already does the same for `maximised`.
5. **[juce_Windowing_wayland_linux.cpp:590-593](https://github.com/topemalheiro/JUCE/blob/32928d231cc46b2133c2dfe72d5cb3e9ccfd5ac7/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L590-L593)** is the getter on the peer, `isMaximisedOrTiled()`.
6. **[juce_Windowing_wayland_linux.cpp:2591-2598](https://github.com/topemalheiro/JUCE/blob/32928d231cc46b2133c2dfe72d5cb3e9ccfd5ac7/modules/juce_gui_basics/native/juce_Windowing_wayland_linux.cpp#L2591-L2598)** is the function block 1 declared. If the peer is a `WaylandComponentPeer` it asks it (block 5); otherwise (an X11 peer, say) it answers `false`. It's the same shape as `isWaylandWindowMaximised()` right above it.

Block 3 is the one that makes the value true or false; blocks 1 and 6 are what applications call; blocks 2, 4 and 5 keep and read it. Nothing here is new logic: `info.maximisedOrTiled` was already computed in [juce_WaylandToplevel_linux.cpp:364-370](https://github.com/topemalheiro/JUCE/blob/32928d231cc46b2133c2dfe72d5cb3e9ccfd5ac7/modules/juce_gui_basics/native/juce_WaylandToplevel_linux.cpp#L364-L370) (maximised, or tiled left, right, top or bottom), and this PR only keeps it and makes it reachable.

How it was tested, and what wasn't, is in the PR description.
