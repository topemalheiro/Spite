# Linux: don't draw while the window can't be seen

[← Overview](README.md) · **PR:** [plugdata-team/plugdata#2431](https://github.com/plugdata-team/plugdata/pull/2431) · plugdata `develop` ← `topemalheiro:pr/host-fixes-hidden` · 1 commit, +66 in 4 files · Linux and BSD only · needs nothing first

## What it does

**What you run into without it.** Some hosts hide a plugin's window instead of closing it, by unmapping it. plugdata isn't told. It keeps drawing every change (number boxes, meters, anything that moves) into a window nobody can see, which costs CPU and GPU for nothing. In my test, a patch updating a number box 30 times a second kept using 434-831 ms of CPU per second while hidden; with the fix, 27-80. A minimised standalone window on X11 is normally unmapped too, so it should be the same case, but I haven't tested that.

**Why plugdata isn't told.** On X11 the host's plugin window contains plugdata's window. When the host unmaps its window, everything inside disappears with it, but X only notifies the window that was unmapped, not the windows inside it. You can still ask X whether a window is "viewable": the window and every window it sits in are mapped.

**What changes.**

- Before recording a frame, plugdata asks X whether its window is viewable. Asking costs a round trip to the X server, so while the answer is yes it asks at most every 200 ms.
- While the window is hidden, it skips drawing and checks again every 50 ms on its own.
- When the window is back, it redraws everything.
- On Wayland nothing changes, because the helper always answers "viewable" there.

## The code, in reading order

1. **[OSUtils.h:43](https://github.com/topemalheiro/plugdata/blob/c616d12010fcdd85d77c15253265b4e958b933d2/Source/Utility/OSUtils.h#L43)** declares the new helper in the Linux part of `OSUtils`.
2. **[OSUtils.cpp:252-261](https://github.com/topemalheiro/plugdata/blob/c616d12010fcdd85d77c15253265b4e958b933d2/Source/Utility/OSUtils.cpp#L252-L261)**: the cases where it can't ask, so it answers "viewable" and nothing changes, as the comment on line 254 says. That's no peer, or a Wayland window (lines 255-256), and no X display or no window id (lines 258-261).
3. **[OSUtils.cpp:263-269](https://github.com/topemalheiro/plugdata/blob/c616d12010fcdd85d77c15253265b4e958b933d2/Source/Utility/OSUtils.cpp#L263-L269)** asks X. It takes the X lock (line 264) and asks for the window's attributes (line 266). If that fails it answers "viewable". Otherwise the answer is `map_state == IsViewable`.
4. **[NVGSurface.h:213-216](https://github.com/topemalheiro/plugdata/blob/c616d12010fcdd85d77c15253265b4e958b933d2/Source/NVGSurface.h#L213-L216)** declares two new private methods of `NVGSurface`, inside `#if JUCE_LINUX || JUCE_BSD`.
5. **[NVGSurface.h:247-250](https://github.com/topemalheiro/plugdata/blob/c616d12010fcdd85d77c15253265b4e958b933d2/Source/NVGSurface.h#L247-L250)** adds two fields: the last answer (starting as "viewable") and when X was last asked, in milliseconds.
6. **[NVGSurface.cpp:529-549](https://github.com/topemalheiro/plugdata/blob/c616d12010fcdd85d77c15253265b4e958b933d2/Source/NVGSurface.cpp#L529-L549)**: `isWindowViewable`, the cached question.
   - Lines 536-538: if the window was viewable and X was asked less than 200 ms ago, answer "viewable" without asking.
   - Lines 539-542: otherwise note the time and ask X (block 3).
   - Lines 543-544: if the window just came back, mark the whole editor as damaged (`invalidateAll()`), so the next frame redraws everything. A hidden window's content isn't kept.
   - Lines 545-546: if it just disappeared, start checking every 50 ms (block 7).
7. **[NVGSurface.cpp:551-557](https://github.com/topemalheiro/plugdata/blob/c616d12010fcdd85d77c15253265b4e958b933d2/Source/NVGSurface.cpp#L551-L557)**: `pollUntilViewable`. In 50 ms, ask again; if the window is still hidden, schedule the same again. It's a chain of one-shot timers (through a `SafePointer`) that ends once the window is back, and that last check also triggers the full redraw. It's needed because a patch that isn't changing asks for no frames at all, so nothing else would ever check.
8. **[NVGSurface.cpp:690-694](https://github.com/topemalheiro/plugdata/blob/c616d12010fcdd85d77c15253265b4e958b933d2/Source/NVGSurface.cpp#L690-L694)**: at the top of `recordFrame`, if the window can't be seen, return without drawing. The damage stays in `invalidArea`, so nothing is lost: it's drawn once the window is back.

Block 8 is the actual fix. Blocks 2 and 3 ask X, block 6 keeps the asking cheap, and block 7 makes sure the window is redrawn when it comes back.

How it was tested, and what wasn't, is in the PR description.
