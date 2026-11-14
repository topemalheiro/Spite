# plugdataMaximize101: tiled windows lose the shadow margin, and double-click stops displacing the window (plugdata PR 12)

**The links expect** `~/Projects/plugdata-prs/maximize` to be checked out on `pr/linux-maximize` (three commits on top of upstream `develop`, last one `c825f35b1`), with its JUCE folder on commit `32928d231c`.

## What this PR is, in plain words

**What you run into without it.** Two things in plugdata's own window on Linux with Wayland (KDE, for example). I reproduced both in a nested KWin with two virtual screens, on upstream `develop`:

- **Half-tiled window keeps its margin.** Press Meta+Left or Meta+Right and the window fills half the screen, but plugdata still shows an 18 pixel empty ring around it, rounded corners and a drop shadow, so you see black gaps at the screen edge. The margin was 18 after the tile, on both screens.
- **Double-click on the title bar leaves the window displaced.** Double-click maximizes it, but it ended up maximized at position (184,188) instead of (0,0), and restoring it landed in the wrong place (548,194 instead of 460,210).

**Why.**

- plugdata draws its own shadow as an 18 pixel margin around the window, and removes the margin when the window is maximized, because then the compositor places it edge to edge. The code only asked "is it maximized?". A tiled window is placed edge to edge too, but it isn't "maximized", so the margin stayed.
- The second click of a double-click did two jobs: it maximized the window and it also started dragging the window. On Wayland the compositor then began moving the window while it was still being resized, and the window ended up displaced.

**What the PR changes.** Three commits, +37 lines and -5 in 5 files:

- A pointer bump so the plugdata code can see the new JUCE function (needs PR 3).
- A new question, "maximized or tiled?", used where the margin, the shadow and the rounded corners are decided.
- A `return` after the double-click toggle, so no drag follows.

Linux and BSD only. Windows and macOS don't change: for them the new question is defined as the old one (`isMaximised()`), and the double-click change is inside `#if JUCE_LINUX || JUCE_BSD`. On X11 (and XWayland) nothing changes either: KWin doesn't tell X11 windows they're tiled, so plugdata can't know.

**What it needs first.** PR 3, the JUCE maximize PR. The new question on Wayland calls `isWaylandWindowMaximisedOrTiled()`, which exists only in that PR. Open PR 3 first so this one can link to it. The first commit points plugdata's JUCE at PR 3's commit, and has to be changed to the merged JUCE commit before this one is merged.

## The code, in reading order

### Commit 1, `f013c7461`: point JUCE at the commit with the new function

1. **JUCE submodule pointer** (`Libraries/JUCE`, a folder that is really a link to another repository at one exact commit, so there are no lines to open). The change is one line: from upstream's JUCE commit `60c451d55b` to `32928d231c`, the PR 3 commit. Without it, block 4 below wouldn't compile, because the function it calls wouldn't exist. `.gitmodules` (the file with the repository's address) isn't touched.
   Python: bumping a pin in `requirements.txt` from one version to the next.

### Commit 2, `f3e89ae42`: take the margin off tiled windows

2. **[OSUtils.h:42](../../plugdata-prs/maximize/Source/Utility/OSUtils.h#L42)** announces a new helper, `isLinuxWindowMaximisedOrTiled`, right under the existing `isLinuxWindowMaximised`. Only the first line, ending in `;`: its code is block 3 (see "One function, written twice" in [juceMaximize101.md](juceMaximize101.md)).
   Python: a method stub with no body in a `class OSUtils`.
3. **[OSUtils.cpp:339-346](../../plugdata-prs/maximize/Source/Utility/OSUtils.cpp#L339-L346)** is that helper with its code. On Wayland it asks JUCE (the PR 3 function). Otherwise, which here means X11, it falls back to the old "is it maximized?" check, because X11 can't say anything about tiling (the comment on line 340 says so).
   Python: `def is_linux_window_maximised_or_tiled(peer): if is_wayland_window(peer): return juce.is_wayland_window_maximised_or_tiled(peer); return is_linux_window_maximised(peer)`.
4. **[PlugDataWindow.h:546-562](../../plugdata-prs/maximize/Source/Standalone/PlugDataWindow.h#L546-L562)** adds `isMaximisedOrTiled()` to plugdata's window, a sibling of the existing `isMaximised()` just above it. On Linux/BSD:
   - If a maximize request was just made by clicking the button and the window manager hasn't answered yet (`hasPendingLinuxMaximisedState`), it answers with what was requested. This is the same shortcut `isMaximised()` has, so the layout doesn't flicker on X11.
   - Otherwise it asks the helper from block 3 about the real window.
   - With no real window yet, it falls back to `isFullScreen()`.
   On every other system it is just `isMaximised()`, the old behaviour (see "Only on Linux" below).
   Python: `def is_maximised_or_tiled(self): if LINUX: if self.pending: return self.pending_state; if peer: return helper(peer); return self.is_full_screen()  else: return self.is_maximised()`.
5. **[PlugDataWindow.h:603](../../plugdata-prs/maximize/Source/Standalone/PlugDataWindow.h#L603)** is in `paint()`, which draws the window's background. The soft drop shadow is only drawn when the window is not maximized or tiled (it was `!isMaximised()` before).
   Python: `if draw_window_shadow and not native_titlebar and not self.is_maximised_or_tiled():`.
6. **[PlugDataWindow.h:620](../../plugdata-prs/maximize/Source/Standalone/PlugDataWindow.h#L620)** is the same change in `paintOverChildren()`, which draws on top: the thin rounded outline around the window is only drawn when it's not maximized or tiled. Otherwise the `else if` right under it draws a plain square 1 pixel border (edge to edge).
   Python: same `if` as block 5.
7. **[PlugDataWindow.h:758](../../plugdata-prs/maximize/Source/Standalone/PlugDataWindow.h#L758)** is in `getMargin()`: it returns the size of the empty ring around the content, 18 pixels or 0. This is the line that matters most: tiled windows now get 0 and the content fills the whole window. Other code reads this number too: the title bar buttons place themselves from it ([PlugDataWindow.h:647-650](../../plugdata-prs/maximize/Source/Standalone/PlugDataWindow.h#L647-L650)) and the content is shrunk by it ([PlugDataWindow.h:824](../../plugdata-prs/maximize/Source/Standalone/PlugDataWindow.h#L824)), so they all follow when it becomes 0.
   Python: `return 0 if owner.is_maximised_or_tiled() else 18`.
8. **[PluginEditor.cpp:2024](../../plugdata-prs/maximize/Source/PluginEditor.cpp#L2024)** is in `wantsRoundedCorners()`, which says whether the editor draws rounded corners. A tiled window sits against the screen edge, so no round corners there either. It was `!window->isMaximised()` before.
   Python: `return not window.native_titlebar() and not window.is_maximised_or_tiled() and can_use_semi_transparent_windows()`.

### Commit 3, `c825f35b1`: double-click doesn't start a drag

9. **[PluginEditor.cpp:889-896](../../plugdata-prs/maximize/Source/PluginEditor.cpp#L889-L896)** is in `mouseDown()`, which runs on every click. Lines 876-886 handle a double-click (`getNumberOfClicks() >= 2`): on macOS they resize by hand; everywhere else line 889 calls `maximiseButtonPressed()`. Then lines 891-895, the new ones, add: on Linux/BSD, stop right there with `return`. Before, the code carried on to the lines below, which start dragging the window (`startDraggingWindow`) because the click was in the title bar. So the same second click maximized the window and started moving it. Now it only maximizes. The comment on lines 892-893 says why.
   Python: `if event.clicks >= 2: window.maximise_button_pressed(); if sys.platform.startswith(("linux", "freebsd")): return` followed by the drag code the `return` now skips.

Block 7 is the actual fix of the margin bug, and block 9 is the actual fix of the double-click bug. Blocks 2 to 6 and 8 feed block 7 (and 8 does the same for the corners); block 1 only lets it build.

## Only on Linux: `#if ... #else` (blocks 4, 7 and 9)

```cpp
#if JUCE_LINUX || JUCE_BSD
    ...Linux code...
#else
    return isMaximised();
#endif
```

`#if` is not a normal `if`. It runs before the compiler, and deletes whatever branch doesn't apply to the system being built. On Windows, only the `#else` part is in the program; on Linux, only the first part. So the Linux code can use things that don't exist elsewhere (the Wayland function from PR 3, for example) without breaking the Windows build, and the Windows program is exactly as before. `||` is "or". `#    if` with spaces in block 9 is the same, indented, because it's nested inside another `#if`.
Python: it's closest to a build-time `if sys.platform == "linux":` that actually removes the other branch from the file before it runs.

## A method that only reads: `const` (block 4)

```cpp
bool isMaximisedOrTiled() const
```

`const` after a method's name promises the method doesn't change the object. It's what lets other `const` code call it (`paint`'s caller, `getMargin() const`). Python has no such promise; the closest is a read-only `@property`.

## Early exit: `return;` in a function with no result (block 9)

`mouseDown` is declared `void`: it gives back nothing. A bare `return;` just leaves it, skipping every line below. That is the whole trick of block 9: the drag-starting lines below are still there; the `return` makes sure they don't run after a double-click on Linux.
Python: a bare `return` in a function that returns `None`.

## Questions that came up

None yet.
