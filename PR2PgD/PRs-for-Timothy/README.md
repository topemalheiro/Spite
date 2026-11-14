# plugdata pull requests: overview

Thirteen pull requests, split by feature so each one can be reviewed, merged or turned down on its own. Nine go to plugdata, two to plugdata-pd and two to your JUCE fork. Most are small fixes; the last one, Detective Mode, is a feature.

Each PR's description says what it fixes, how it was tested and what wasn't tested. The notes in this folder add one thing: a walk through the changed code in reading order, with a link to the exact lines and a sentence on which block is the actual fix. They're there if they save you time; the PRs stand without them.

## The pull requests

| # | Pull request | What it does | Size | Needs first | Code walk-through |
| -- | -- | -- | -- | -- | -- |
| 1 | [plugdata-team/plugdata-pd#14](https://github.com/plugdata-team/plugdata-pd/pull/14) | **pure-data:** a hook that hands plugdata every message sent through a connection, in order. Only Detective Mode (13) uses it. | +25 | nothing | [01-pd-trace-hook.md](01-pd-trace-hook.md) |
| 2 | [plugdata-team/plugdata-pd#15](https://github.com/plugdata-team/plugdata-pd/pull/15) | **pure-data:** `canvas_saveto()` looks up object indices in a table, so saving a big canvas is linear instead of quadratic. | +78 −2 | nothing | [02-pd-save-canvas-index.md](02-pd-save-canvas-index.md) |
| 3 | [plugdata-team/JUCE#8](https://github.com/plugdata-team/JUCE/pull/8) | **JUCE:** `isWaylandWindowMaximisedOrTiled()`, so an app can tell a tiled Wayland window from a floating one. | +25 | nothing | [03-juce-wayland-tiled-state.md](03-juce-wayland-tiled-state.md) |
| 4 | [plugdata-team/JUCE#9](https://github.com/plugdata-team/JUCE/pull/9) | **JUCE:** a Wayland popup starts with its window's scale. Fixes menus opening at about half size on fractionally scaled screens. | +56 −1 | nothing | [04-juce-wayland-popup-scale.md](04-juce-wayland-popup-scale.md) |
| 5 | [plugdata-team/plugdata#2426](https://github.com/plugdata-team/plugdata/pull/2426) | A long `[popmenu]` item no longer runs under the arrows. | +2 −1 | nothing | [05-popmenu-text-under-arrows.md](05-popmenu-text-under-arrows.md) |
| 6 | [plugdata-team/plugdata#2427](https://github.com/plugdata-team/plugdata/pull/2427) | Centred and right-aligned text in a fixed-width `[note]` lines up with the box. | +32 −4 | nothing | [06-note-fixed-width-alignment.md](06-note-fixed-width-alignment.md) |
| 7 | [plugdata-team/plugdata#2428](https://github.com/plugdata-team/plugdata/pull/2428) | An Inspector drop-down applies to all selected objects when they differ. | +17 −5 | nothing | [07-inspector-mixed-choices.md](07-inspector-mixed-choices.md) |
| 8 | [plugdata-team/plugdata#2429](https://github.com/plugdata-team/plugdata/pull/2429) | A read-only `[knob]` is drawn greyed out. A look choice, not a bug fix. | +21 −5 | nothing | [08-knob-read-only-greyed.md](08-knob-read-only-greyed.md) |
| 9 | [plugdata-team/plugdata#2430](https://github.com/plugdata-team/plugdata/pull/2430) | **Linux:** the plugins no longer crash hosts that export Lua (Ardour). Two linker options. | +11 | nothing | [09-lua-link-flags.md](09-lua-link-flags.md) |
| 10 | [plugdata-team/plugdata#2431](https://github.com/plugdata-team/plugdata/pull/2431) | **Linux:** no drawing while the window is unmapped (a hidden plugin window, a minimised standalone on X11). | +66 | nothing | [10-hidden-window-drawing.md](10-hidden-window-drawing.md) |
| 11 | [plugdata-team/plugdata#2432](https://github.com/plugdata-team/plugdata/pull/2432) | Saving the plugin state and opening big subpatches no longer block the audio thread. Also fixes open subpatch tabs being saved as extra patches. | +50 −26 | 2 | [11-audio-stalls-on-save.md](11-audio-stalls-on-save.md) |
| 12 | [plugdata-team/plugdata#2433](https://github.com/plugdata-team/plugdata/pull/2433) | **Linux:** a tiled Wayland window loses the shadow margin, and a title bar double-click no longer leaves the window displaced. | +37 −5 | 3 | [12-linux-maximize.md](12-linux-maximize.md) |
| 13 | [plugdata-team/plugdata#2434](https://github.com/plugdata-team/plugdata/pull/2434) | **Detective Mode:** every message Pd sends through a connection is played back as a dot travelling along it, one at a time, in execution order. Off by default. | +902 −10 | 1 | [13-detective-mode.md](13-detective-mode.md) |

Sizes are lines added and removed, as GitHub counts them. For 11, 12 and 13 that includes one line each way for the submodule pointer.

## Which ones depend on each other

- **1 to 10 need nothing.** They can be merged in any order, or not at all, without affecting the others.
- **11, 12 and 13 each need a library PR** (2, 3 and 1). Each of them moves a submodule pointer to the library PR's commit on my fork, so that it builds. That pointer has to become the merged commit before the plugdata PR is merged, and I'll update the branch when the library PR is in. If you squash-merge a library PR, its commit changes; I'll point the plugdata PR at the new one.
- **11 and 13 both move `Libraries/pure-data`** (to 2's commit and to 1's commit). Whichever of them is merged second needs its pointer on plugdata-pd `master` once both pure-data PRs are in. The two pure-data PRs don't clash with each other.
- **3 and 4 both change `juce_Windowing_wayland_linux.cpp`,** in different places. They merge cleanly with each other.
- **4 has no plugdata PR.** plugdata gets the fix the next time its JUCE pointer moves.
- Everything else merges cleanly. I trial-merged every pair of the nine plugdata branches: the pure-data pointer of 11 and 13 is the only conflict.

## A sensible order

The numbers are the order I'd go in: the library PRs first, because 11 to 13 wait on them; then the small fixes, simplest first; then the three that need a library PR. Detective Mode is last because it's the biggest review. PR 8 changes a look rather than fixing a bug, so it's the one most open to discussion.

## What they have in common

- **Built and tested on Linux only** (Arch, KWin 6, Wayland and X11 through Xwayland, mostly in a nested KWin). Nothing here was built on macOS, Windows or iOS, with one exception: an earlier version of the Detective Mode branch built on Windows and on macOS (the legacy target) in my fork's CI on 12 November. Each description lists what wasn't tested.
- **Where they come from:** I run plugdata's LV2 in an Ardour-based DAW on Linux, with very large patches (thousands of objects in one). Most of these problems showed up there. Each one was then reproduced on plain `develop`.
- **How they were written:** with Claude Code, as each description says. The code links in these notes point at the commit each PR was opened with.

## How to read a note

Each note has the same parts:

1. **What it does:** what you run into without the PR, why, and what changes.
2. **The code, in reading order:** numbered blocks, each with a link to the lines on GitHub and what those lines do. The order is the one that makes the change easiest to follow, which isn't always file order.
3. **One closing sentence** saying which block is the actual fix and which blocks only connect it.
