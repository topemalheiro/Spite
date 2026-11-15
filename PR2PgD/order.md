# Order to open the PRs

| #    | PR'd | PR                                                                        | 101 note                                     | desktop | needs   | can go out together with           |
| ---- | ---- | ------------------------------------------------------------------------- | -------------------------------------------- | ------- | ------- | ---------------------------------- |
| 1    | **PR'd:** [plugdata-pd #14](https://github.com/plugdata-team/plugdata-pd/pull/14) | plugdata-pd: Detective Mode trace hook                                    | [pdHook101](pdHook101.md)                     | Spite 1 | nothing | anything that needs nothing        |
| 2    | **PR'd:** [plugdata-pd #15](https://github.com/plugdata-team/plugdata-pd/pull/15) | plugdata-pd: save a big canvas in linear time                             | [saveCanvas101](saveCanvas101.md)             | Spite 4 | nothing | anything that needs nothing        |
| 3    | **PR'd:** [JUCE #8](https://github.com/plugdata-team/JUCE/pull/8) | JUCE: Linux maximize                                                      | [juceMaximize101](juceMaximize101.md)         | Spite 2 | nothing | anything that needs nothing        |
| 4    | not sent, stays here | plugdata: add an "Oval outline" option to [button]                        | [ovalButton101](ovalButton101.md)             | Spite 3 | 14      | 11, 12, 13 and 16, once 14 is open |
| 5    | **PR'd:** [plugdata #2426](https://github.com/plugdata-team/plugdata/pull/2426), **in develop** as 20f9c5f34, closed | plugdata: fix popup menu text running under its arrows                    | [popupMenu101](popupMenu101.md)               | Spite 3 | nothing | anything that needs nothing        |
| 6    | **PR'd:** [plugdata #2427](https://github.com/plugdata-team/plugdata/pull/2427) and #2435, **in develop** as Timothy's rework cef81dfb6, closed | plugdata: fix centred and right-aligned text in fixed-width notes (+ #2435) | [noteAlignment101](noteAlignment101.md)       | Spite 3 | nothing | anything that needs nothing        |
| 7    | **PR'd:** [plugdata #2428](https://github.com/plugdata-team/plugdata/pull/2428) | plugdata: fix Inspector choices not applying when selected objects differ | [inspectorChoices101](inspectorChoices101.md) | Spite 3 | nothing | anything that needs nothing        |
| 8    | **PR'd:** [plugdata #2429](https://github.com/plugdata-team/plugdata/pull/2429) | plugdata: draw read-only knobs greyed out                                 | [readonlyKnob101](readonlyKnob101.md)         | Spite 3 | nothing | anything that needs nothing        |
| 9    | **PR'd:** [plugdata #2430](https://github.com/plugdata-team/plugdata/pull/2430), **in develop** as 48d6396dd, closed | plugdata: Lua link flags                                                  | [luaFlags101](luaFlags101.md)                 | Spite 4 | nothing | anything that needs nothing        |
| 10   | **PR'd:** [plugdata #2431](https://github.com/plugdata-team/plugdata/pull/2431) | plugdata: hidden-window fix                                               | [hiddenWindow101](hiddenWindow101.md)         | Spite 4 | nothing | anything that needs nothing        |
| 11   | **PR'd:** [plugdata #2432](https://github.com/plugdata-team/plugdata/pull/2432) | plugdata: plugin-host stalls fix                                          | [hostStalls101](hostStalls101.md)             | Spite 4 | 2       | 4, 12, 13 and 16, once 2 is open   |
| 12   | **PR'd:** [plugdata #2433](https://github.com/plugdata-team/plugdata/pull/2433) | plugdata: Linux maximize                                                  | [plugdataMaximize101](plugdataMaximize101.md) | Spite 2 | 3       | 4, 11, 13 and 16, once 3 is open   |
| 13   | **PR'd:** [plugdata #2434](https://github.com/plugdata-team/plugdata/pull/2434) | plugdata: Detective Mode                                                  | [detectiveMode101](detectiveMode101.md)       | Spite 1 | 1       | 4, 11, 12 and 16, once 1 is open   |
| 14   | not sent, stays here | ELSE: "ovaloutline" option for [button]                                   | [ovalButton101](ovalButton101.md)             | Spite 3 | nothing | anything that needs nothing        |
| 15   | not sent, stays here | ELSE: "style" option for [knob], for plugdata's tiny dial                 | [knobStyle101](knobStyle101.md)               | Spite 3 | nothing | anything that needs nothing        |
| 16  | not sent, stays here | plugdata: add a "Style" option to [knob], the knob or a tiny dial         | [knobStyle101](knobStyle101.md)               | Spite 3 | 15      | 4, 11, 12 and 13, once 15 is open  |
| 17   | **PR'd:** [JUCE #9](https://github.com/plugdata-team/JUCE/pull/9) | JUCE: Wayland popups start with their window's scale (tiny dropdowns)     | [popupScale101](popupScale101.md)             | Spite 2 | nothing | anything that needs nothing        |

**PR'd (2026-11-14):** 13 of the 17 were opened upstream that day: 1 to 3, 5 to 13 and 17. The links are in the table. 4, 14, 15 and 16 (the "Oval outline" option and the knob's "Style", with their ELSE halves) were not sent. They stay in your own plugdata (the fork's `main`) and can be sent later.

**15 November, PR 6:** Timothy found that a note that wasn't resized still lets right-aligned text run past its box (it was like that before too). The fix for that is a separate, optional PR, [plugdata #2435](https://github.com/plugdata-team/plugdata/pull/2435): #2427 plus one commit (2b308344f). Timothy said he's restructuring this himself, so #2427 stays exactly as he tested it. Your plugdata (`main`) and PgD4S already have the fix.

**15 November, in develop:** Timothy committed three of these to upstream `develop` himself, under your name, and closed their PRs: 5 (popup menu, 20f9c5f34, our line without its comment), 9 (Lua link flags, 48d6396dd, identical to ours) and 6 (notes, cef81dfb6, his own rework that covers resized and never-resized notes, so #2435 is closed too). Your plugdata's `main` now has upstream's versions of all three (merge 76ee3f88c). PgD4S keeps its own versions, which it already had. On 8 (read-only knob), dromer commented that it departs from vanilla Pd and should be discussed with porres.

**The folder for Timothy:** [PRs-for-Timothy](PRs-for-Timothy/README.md) holds his copy of this list and of the 13 notes: no Python lines, no C lessons, and code links that open on GitHub. There the JUCE dropdown fix (17 here) is number 4, so his list runs 1 to 13; all the others keep their numbers.

**101 note:** the dumbed-down study note for that PR, to read next to its code.

**needs:** that PR has to be opened before this one, so this one can link to it, and merged before this one. A PR that needs nothing can go out at any time.

**Why this order:** the library PRs go first, because the plugdata PRs that need them wait on them: 1 to 3, then 14, 15 and 17. The small fixes come next, simplest first. The read-only knob (8) changes a look rather than fixing a bug, so it may get discussion. Detective Mode (13) is the biggest review, so it goes last of the first thirteen.

**14 to 17 were added on 2026-11-14.** They got the next free numbers, so the numbers the notes already use stay right. PR 4 changed that day too: it now adds an Inspector option, "Oval outline", instead of always drawing an oval button's box round. ELSE's [button] saves that option, so 4 now needs ELSE PR 14. The knob's Style (16) is saved by ELSE's [knob] the same way, so it needs ELSE PR 15. The JUCE fix (17) has no plugdata PR: plugdata gets it the next time its maintainers move JUCE.

**Still open:** the subpatch-save fix is inside 11 for now. If you split it out, it becomes PR 18, which goes right after 11. The host-fixes agent recommends keeping it inside 11.

**A possible later PR (found 2026-11-14, not measured, in no PR):** plugdata's own autosave runs on the audio thread. Its timer queues the save, and the audio callback runs that queue (Autosave.h:92, PluginProcessor.cpp:992 and 1054). So with a big patch that has unsaved changes, the audio waits for the whole save each time the autosave fires. PRs 2 and 11 only make that save faster. The host-fixes agent found it.

## Do they have to be opened in this order for the maintainers to merge them correctly?

Only where a PR needs another one.

- **Everything except 4, 11, 12, 13 and 16 needs nothing.** Those can be opened and merged in any order. The numbers are just a sensible order to send them in.
- **4, 11, 12, 13 and 16 each need a library PR** (14, 2, 3, 1 and 15). Open the library PR first, so the plugdata PR's description can link to it. The library PR has to be merged before the plugdata PR, but the plugdata PR doesn't have to wait for that merge to be opened.
- **Their checks won't fail because of that.** plugdata's CI downloads the libraries at the exact commit a PR points at, and GitHub serves your forks' commits through plugdata-team's own address. Tested on 2026-11-13 with the hook commit (caeb1359) and the JUCE maximize commit (32928d23). ELSE should work the same way, since topemalheiro/pd-else is a fork of timothyschoen/pd-else, but that hasn't been tested.
- **If a library PR is squash-merged,** its commit changes, so the plugdata PR that needs it must point at the new commit before it's merged. We update that PR's branch when it happens.
- **Two clashes over library pointers:**
  - 11 and 13 both move plugdata's pure-data pointer. Whichever of them is merged second must point it at plugdata-pd's `master` once both Pd PRs (1 and 2) are in. The two Pd PRs don't clash with each other.
  - 4 and 16 both move plugdata's pd-else pointer. Whichever of them is merged second must point it at ELSE's `plugdata-rebased` once both ELSE PRs (14 and 15) are in. The two ELSE PRs don't clash with each other.
- **8 and 16 both change how a knob is drawn.** Whichever of them is merged second also takes on "Grey out read-only tiny dials too", two words in KnobObject.h, so read-only tiny dials are greyed out as well.
- **Everything else merges cleanly in any order.** The host-fixes agent trial-merged every pair of PRs that change the same files: hidden-window with Linux maximize (both change OSUtils), Lua flags with maximize, stalls with maximize, hidden-window with Detective Mode, and stalls with Detective Mode, where PluginProcessor.cpp merges by itself. The presentation-objects agent built and tested 16 together with the five GUI fixes (4 to 8). Pairs with 14, 15 and 17 haven't been trial-merged.
