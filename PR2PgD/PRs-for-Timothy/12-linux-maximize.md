# Linux: remove the window shadow inset when tiled on Wayland, and fix title bar double-click

[← Overview](README.md) · **PR:** [plugdata-team/plugdata#2433](https://github.com/plugdata-team/plugdata/pull/2433) · plugdata `develop` ← `topemalheiro:pr/linux-maximize` · 3 commits, +37 −5 in 5 files (one is the JUCE pointer) · Linux and BSD only · needs [plugdata-team/JUCE#8](https://github.com/plugdata-team/JUCE/pull/8) first ([03-juce-wayland-tiled-state.md](03-juce-wayland-tiled-state.md))

## What it does

**What you run into without it.** Two things in the standalone window on Linux with Wayland. I reproduced both on `develop`, in a nested KWin with two outputs:

- **A half-tiled window keeps its margin.** Press Meta+Left or Meta+Right and the window fills half the screen, but plugdata still shows the 18 px empty ring around it, the rounded corners and the drop shadow, so you see gaps at the screen edge.
- **Double-clicking the title bar leaves the window displaced.** It maximises, but it ended up maximised at (184,188) instead of (0,0), and restoring it landed in the wrong place (548,194 instead of 460,210).

**Why.**

- plugdata draws its own shadow as an 18 px margin around the window, and removes the margin when the window is maximised, because then the compositor places it edge to edge. The code only asked "is it maximised?". A tiled window is placed edge to edge too, but it isn't maximised, so the margin stayed.
- The second click of a double-click did two jobs: it maximised the window and it also started dragging it. On Wayland the compositor then began moving the window while it was still being resized.

**What changes.** Three commits:

- A pointer bump so the plugdata code can see the new JUCE function ([plugdata-team/JUCE#8](https://github.com/plugdata-team/JUCE/pull/8)).
- A new question, "maximised or tiled?", used where the margin, the shadow and the rounded corners are decided.
- A `return` after the double-click toggle, so no drag follows.

Windows and macOS don't change: for them the new question is defined as the old one (`isMaximised()`), and the double-click change is inside `#if JUCE_LINUX || JUCE_BSD`. On X11 (and Xwayland) nothing changes either: KWin sets no tiled state on X11 windows, so plugdata can't know.

**What it needs first.** [plugdata-team/JUCE#8](https://github.com/plugdata-team/JUCE/pull/8), the JUCE PR that adds `isWaylandWindowMaximisedOrTiled()`. The first commit points `Libraries/JUCE` at that PR's commit on my fork, and has to be changed to the merged JUCE commit before this one is merged.

## The code, in reading order

### Commit 1, `f013c7461`: point JUCE at the commit with the new function

1. **`Libraries/JUCE`**: the submodule pointer moves from upstream's `60c451d55b` to `32928d231c`, the JUCE PR's commit. Without it, block 3 below wouldn't compile. `.gitmodules` isn't touched.

### Commit 2, `f3e89ae42`: take the margin off tiled windows

2. **[OSUtils.h:42](https://github.com/topemalheiro/plugdata/blob/c825f35b14bc2c547892e1ac11710e430ff88db3/Source/Utility/OSUtils.h#L42)** declares a new helper, `isLinuxWindowMaximisedOrTiled`, right under the existing `isLinuxWindowMaximised`.
3. **[OSUtils.cpp:339-346](https://github.com/topemalheiro/plugdata/blob/c825f35b14bc2c547892e1ac11710e430ff88db3/Source/Utility/OSUtils.cpp#L339-L346)** is that helper. On Wayland it asks JUCE (the new function). Otherwise, which here means X11, it falls back to `isLinuxWindowMaximised`, because X11 can't say anything about tiling (the comment on line 340 says so).
4. **[PlugDataWindow.h:546-562](https://github.com/topemalheiro/plugdata/blob/c825f35b14bc2c547892e1ac11710e430ff88db3/Source/Standalone/PlugDataWindow.h#L546-L562)** adds `isMaximisedOrTiled()` to the window, a sibling of the existing `isMaximised()` just above it. On Linux/BSD:
   - If a maximise request was just made and the window manager hasn't answered yet (`hasPendingLinuxMaximisedState`), it answers with what was requested. This is the same shortcut `isMaximised()` has.
   - Otherwise it asks the helper from block 3 about the peer.
   - With no peer yet, it falls back to `isFullScreen()`.

   On every other system it is just `isMaximised()`, the old behaviour.
5. **[PlugDataWindow.h:603](https://github.com/topemalheiro/plugdata/blob/c825f35b14bc2c547892e1ac11710e430ff88db3/Source/Standalone/PlugDataWindow.h#L603)** is in `paint()`. The drop shadow is only drawn when the window is not maximised or tiled (it was `!isMaximised()` before).
6. **[PlugDataWindow.h:620](https://github.com/topemalheiro/plugdata/blob/c825f35b14bc2c547892e1ac11710e430ff88db3/Source/Standalone/PlugDataWindow.h#L620)** is the same change in `paintOverChildren()`: the thin rounded outline around the window is only drawn when it's not maximised or tiled. Otherwise the `else if` right under it draws a plain square 1 px border.
7. **[PlugDataWindow.h:758](https://github.com/topemalheiro/plugdata/blob/c825f35b14bc2c547892e1ac11710e430ff88db3/Source/Standalone/PlugDataWindow.h#L758)** is in `getMargin()`: it returns the size of the empty ring around the content, 18 px or 0. This is the line that matters most: tiled windows now get 0 and the content fills the whole window. Other code reads this number too: the title bar buttons place themselves from it ([PlugDataWindow.h:647-650](https://github.com/topemalheiro/plugdata/blob/c825f35b14bc2c547892e1ac11710e430ff88db3/Source/Standalone/PlugDataWindow.h#L647-L650)) and the content is shrunk by it ([PlugDataWindow.h:824](https://github.com/topemalheiro/plugdata/blob/c825f35b14bc2c547892e1ac11710e430ff88db3/Source/Standalone/PlugDataWindow.h#L824)), so they all follow when it becomes 0.
8. **[PluginEditor.cpp:2024](https://github.com/topemalheiro/plugdata/blob/c825f35b14bc2c547892e1ac11710e430ff88db3/Source/PluginEditor.cpp#L2024)** is in `wantsRoundedCorners()`. A tiled window sits against the screen edge, so no rounded corners there either. It was `!window->isMaximised()` before.

### Commit 3, `c825f35b1`: double-click doesn't start a drag

9. **[PluginEditor.cpp:889-896](https://github.com/topemalheiro/plugdata/blob/c825f35b14bc2c547892e1ac11710e430ff88db3/Source/PluginEditor.cpp#L889-L896)** is in `mouseDown()`. Lines 876-886 handle a double-click (`getNumberOfClicks() >= 2`): on macOS they resize by hand; everywhere else line 889 calls `maximiseButtonPressed()`. Then lines 891-895, the new ones, add: on Linux/BSD, `return`. Before, the code carried on to the lines below, which start dragging the window (`startDraggingWindow`) because the click was in the title bar. So the same second click maximised the window and started moving it. Now it only maximises.

Block 7 is the actual fix of the margin bug, and block 9 is the actual fix of the double-click bug. Blocks 2 to 6 and 8 feed block 7 (and 8 does the same for the corners); block 1 only lets it build.

How it was tested, and what wasn't, is in the PR description.
