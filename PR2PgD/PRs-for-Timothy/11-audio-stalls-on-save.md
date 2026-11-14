# Don't block the audio thread while saving the plugin state or opening subpatches

[← Overview](README.md) · **PR:** [plugdata-team/plugdata#2432](https://github.com/plugdata-team/plugdata/pull/2432) · plugdata `develop` ← `topemalheiro:pr/host-fixes-stalls` · 4 commits, +50 −26 in 5 files (one is the pure-data pointer) · needs [plugdata-team/plugdata-pd#15](https://github.com/plugdata-team/plugdata-pd/pull/15) first ([02-pd-save-canvas-index.md](02-pd-save-canvas-index.md))

## What it does

**What you run into without it.**

- Saving in a DAW (a host's autosave included) can make the audio drop out. The bigger the patch, the longer the gap: with an 8000-object patch the audio stopped for 150-380 ms on every save, when one audio block lasts 5.3 ms.
- Opening a patch with a big graph-on-parent subpatch stops the audio too: 40 ms with 2000 objects inside it, 0.7 s with 8000.
- If a subpatch is open in its own tab when the DAW saves, reopening the session gives you an extra patch: a loose copy of that subpatch, whose objects then run twice.

**Why.** All of it is about how long Pd's lock is held, since the audio thread takes it for every block. Saving held it while it serialised every patch, and opening held it while it built a whole subpatch's GUI.

**What changes**, one commit each:

1. Saving holds the lock only to copy the list of patches and to collect each patch's contents. Turning those into text, the slow part, happens after letting go.
2. Opening decides under the lock what kind of object it is (subpatch, graph-on-parent, array) and builds its GUI after letting go.
3. Saving a top-level patch uses the pure-data PR's index, so the part still under the lock is fast.
4. Saving keeps only real top-level patches, so a subpatch open in a tab isn't saved a second time.

**What it needs first.** The pure-data PR, [plugdata-team/plugdata-pd#15](https://github.com/plugdata-team/plugdata-pd/pull/15), which has to be merged before this one. This PR builds meanwhile, because GitHub serves the commit from my fork through plugdata-team/plugdata-pd's URL too.

## The code, in reading order, commit by commit

### Commit 1: don't hold the lock for the whole save (5b5ab046f)

1. **[PluginProcessor.cpp:1252-1263](https://github.com/topemalheiro/plugdata/blob/0395fdbba553b70d1f8f39ba6900042e231ac0b8/Source/PluginProcessor.cpp#L1252-L1263)** is the start of `getStateInformation`. It takes the lock, copies the open patches into `patchesToSave`, and lets go. Commit 1 had a plain copy here (`auto const patchesToSave = patches;`); lines 1257-1262 are commit 4's version, which keeps only top-level patches (block 11). The copy holds `Patch::Ptr`s, so a patch whose tab is closed during the save stays alive until the save ends.
2. **[PluginProcessor.cpp:1268-1296](https://github.com/topemalheiro/plugdata/blob/0395fdbba553b70d1f8f39ba6900042e231ac0b8/Source/PluginProcessor.cpp#L1268-L1296)** saves from the copy, without the lock. Only lines 1268 and 1275 changed (`patches` became `patchesToSave`); the lock and unlock lines that used to surround this loop are gone.
3. **[Patch.cpp:709-730](https://github.com/topemalheiro/plugdata/blob/0395fdbba553b70d1f8f39ba6900042e231ac0b8/Source/Pd/Patch.cpp#L709-L730)**: `getCanvasContent`, which every save uses (autosave and the Heavy exporter too). It holds the lock only while it collects the patch's contents as a binbuf (lines 714-718). After letting go it turns them into text (lines 720-723); lines 725-729 were already there.
4. **[Interface.h:736-738](https://github.com/topemalheiro/plugdata/blob/0395fdbba553b70d1f8f39ba6900042e231ac0b8/Source/Pd/Interface.h#L736-L738)**: the helper's new signature. `getCanvasBinbuf` hands back the collected binbuf, where `getCanvasContent` wrote the text into two out-parameters.
5. **[Interface.h:811](https://github.com/topemalheiro/plugdata/blob/0395fdbba553b70d1f8f39ba6900042e231ac0b8/Source/Pd/Interface.h#L811)**: `return b;`, where the helper used to make the text and free the binbuf itself.

### Commit 2: create a subpatch's GUI after letting go of the lock (30ca32df2)

6. **[ObjectBase.cpp:647-649](https://github.com/topemalheiro/plugdata/blob/0395fdbba553b70d1f8f39ba6900042e231ac0b8/Source/Objects/ObjectBase.cpp#L647-L649)**: a comment, and an `enum class Kind` variable to remember the decision: Unknown, Array, Graph or Subpatch.
7. **[ObjectBase.cpp:650-660](https://github.com/topemalheiro/plugdata/blob/0395fdbba553b70d1f8f39ba6900042e231ac0b8/Source/Objects/ObjectBase.cpp#L650-L660)**, the locked part, decides what the object is. If the first thing inside is an array, it's an array. If it's graph-on-parent, it's a graph. Otherwise it's a subpatch or an abstraction. These are the same tests as before, laid out flat.
8. **[ObjectBase.cpp:661-666](https://github.com/topemalheiro/plugdata/blob/0395fdbba553b70d1f8f39ba6900042e231ac0b8/Source/Objects/ObjectBase.cpp#L661-L666)**: with the lock released, create the matching object. A `GraphOnParent` builds the GUI of every object inside its subpatch, which is the slow part the audio used to wait for. If the object was deleted in the meantime, `kind` stays `Unknown`, none of the three `return`s run, and the code falls into the next `case`, exactly as it did before.

### Commit 3: use the pure-data PR's index for top-level patches (3aa8c9796)

9. **[Libraries/pure-data](https://github.com/topemalheiro/plugdata-pd/commit/b7edbbb68f0d930f701f3f23cfbfeec90c82b9cd)**: the submodule pointer moves from 267a42a8 (upstream's) to b7edbbb6 (the pure-data PR), so the index functions exist.
10. **[Interface.h:773-788](https://github.com/topemalheiro/plugdata/blob/0395fdbba553b70d1f8f39ba6900042e231ac0b8/Source/Pd/Interface.h#L773-L788)**: plugdata saves top-level patches with its own copy of Pd's save loop, and this gives it the same change as `canvas_saveto()` gets in the pure-data PR. It builds the index (lines 774-775), looks up both ends of each connection (lines 778-779), and frees the index (line 788).

### Commit 4: don't save open subpatch tabs as extra patches (0395fdbba)

11. **[PluginProcessor.cpp:1257-1262](https://github.com/topemalheiro/plugdata/blob/0395fdbba553b70d1f8f39ba6900042e231ac0b8/Source/PluginProcessor.cpp#L1257-L1262)**: while copying the list under the lock, keep only patches with no parent (`!cnv->gl_owner`). A subpatch open in a tab is already saved inside its parent, so it isn't saved again on its own. One thing changes for the user: an open subpatch tab isn't reopened after loading, where before it came back as that wrong extra patch.

Blocks 1 to 3 and 7 to 8 are the actual fixes for the dropouts: the lock is held only for the short parts. Block 10 makes what's still under the lock fast, block 11 is the separate subpatch-save fix, and blocks 4 to 6 and 9 are plumbing.

The timings before and after, how they were measured, and what wasn't tested are in the PR description.
