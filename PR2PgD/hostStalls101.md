# hostStalls101: no audio stalls from saving or opening patches ([plugdata#2432](https://github.com/plugdata-team/plugdata/pull/2432))

**The links expect** `~/Projects/plugdata-prs/host-fixes` to be checked out on `review/host-fixes` (it is). That branch holds all three of my plugdata PRs together. The files this PR changes are identical there to the PR's own branch `pr/host-fixes-stalls`, and its `Libraries/pure-data` is at the same pure-data commit (b7edbbb6), so checking `pr/host-fixes-stalls` out instead gives the same lines.

## What this PR is, in plain words

**What you run into without it.**

- Saving in a DAW (its autosave too) can click the audio. The bigger the patch, the longer the gap: with an 8000-object patch the audio stopped for 150-380 ms on every save, when one audio block lasts 5.3 ms.
- Opening a patch with a big graph-on-parent subpatch stops the audio too: 40 ms with 2000 objects inside it, 0.7 s with 8000. That also happens in the standalone app.
- If a subpatch is open in its own tab when the DAW saves, reopening the session gives you an extra patch: a loose copy of that subpatch, whose objects then run twice.

**The one idea you need: Pd's lock.** plugdata's audio thread takes Pd's lock for every audio block. The GUI and the save code take the same lock to read the patch safely. Whatever holds it, the audio waits for, and a wait longer than a block is a dropout you hear. Python comparison: a `threading.Lock` that the audio loop takes with `with pd_lock:` for every block.

**What the PR changes**, one commit each:

1. Saving holds the lock only to copy the list of patches and to collect each patch's contents. Turning those into text, the slow part, happens after letting go.
2. Opening decides under the lock what kind of box it is (subpatch, graph-on-parent, array) and builds its GUI after letting go.
3. Saving a top-level patch uses the pure-data PR's lookup table (saveCanvas101.md), so the part still under the lock is fast.
4. Saving keeps only real top-level patches, so a subpatch open in a tab isn't saved a second time.

**What it needs first.** The pure-data PR, plugdata-pd#15 (saveCanvas101.md). It has to be merged before this one, and this PR's description says so. This PR builds anyway: GitHub serves the pure-data commit from your fork through plugdata-team's own address.

## The code, in reading order, commit by commit

### Commit 1: don't hold the lock for the whole save (5b5ab046f)

1. **[PluginProcessor.cpp:1252-1263](../../plugdata-prs/host-fixes/Source/PluginProcessor.cpp#L1252-L1263)** is the start of `getStateInformation`, the function a host calls to get plugdata's state when it saves. It takes the lock, copies the open patches into `patchesToSave`, and lets go. Commit 1 had a plain copy here (`auto const patchesToSave = patches;`); lines 1257-1262 are commit 4's version, which keeps only top-level patches (block 11).
   Python: `with pd_lock: patches_to_save = list(self.patches)`.
2. **[PluginProcessor.cpp:1268-1296](../../plugdata-prs/host-fixes/Source/PluginProcessor.cpp#L1268-L1296)** saves from the copy, without the lock. It writes how many patches there are (line 1268), then for each one (line 1275) its text, its file path and plugdata's settings for it, such as split view and plugin mode. Only lines 1268 and 1275 changed (`patches` became `patchesToSave`); the lock and unlock lines that used to surround this loop are gone.
   Python: `out.write_int(len(patches_to_save))`, then `for patch in patches_to_save: out.write(patch.get_canvas_content())`.
3. **[Patch.cpp:709-730](../../plugdata-prs/host-fixes/Source/Pd/Patch.cpp#L709-L730)**: `getCanvasContent`, which every save, autosave and the Heavy exporter use. It holds the lock only while it collects the patch's contents as Pd's internal list of words, a "binbuf" (lines 714-718). After letting go it turns them into text (lines 720-723); lines 725-729 were already there. See "A lock held for one block" and "binbuf". The shorter lock doesn't help plugdata's own autosave: it's queued with `enqueueFunctionAsync` ([Autosave.h:92](../../plugdata-prs/host-fixes/Source/Utility/Autosave.h#L92)), which runs it on the audio thread itself, so the audio waits for the whole save anyway. It only gets faster, from commit 3 and the pure-data PR.
   Python: `with pd_lock: words = interface.get_canvas_binbuf(canvas)`, then, outside the `with`, `return " ".join(words)`.
4. **[Interface.h:736-738](../../plugdata-prs/host-fixes/Source/Pd/Interface.h#L736-L738)**: the helper's new first line. It now hands back the collected binbuf instead of writing text into two variables you pass in (see "Returning two things through addresses").
   Python: `def get_canvas_binbuf(canvas) -> Binbuf:` instead of `def get_canvas_content(canvas, out_text, out_length):`.
5. **[Interface.h:811](../../plugdata-prs/host-fixes/Source/Pd/Interface.h#L811)**: `return b;`, where the helper used to make the text and free the binbuf itself.
   Python: `return words`.

### Commit 2: create a subpatch's GUI after letting go of the lock (30ca32df2)

6. **[ObjectBase.cpp:647-649](../../plugdata-prs/host-fixes/Source/Objects/ObjectBase.cpp#L647-L649)**: a comment, and a variable to remember the decision. `Kind` can only be Unknown, Array, Graph or Subpatch (see "enum class, and falling into the next case").
   Python: `class Kind(Enum): UNKNOWN = 0; ARRAY = 1; GRAPH = 2; SUBPATCH = 3`, then `kind = Kind.UNKNOWN`.
7. **[ObjectBase.cpp:650-660](../../plugdata-prs/host-fixes/Source/Objects/ObjectBase.cpp#L650-L660)**, the locked part, held from line 650 to the `}` on line 660, decides what the box is. If the first thing inside is an array, it's an array. If it's set to show its contents on its parent, it's a graph-on-parent. Otherwise it's a subpatch or an abstraction. These are the same tests as before, laid out flat.
   Python: `with pd_lock: kind = ARRAY if first_is_array else GRAPH if canvas.is_graph else SUBPATCH`.
8. **[ObjectBase.cpp:661-666](../../plugdata-prs/host-fixes/Source/Objects/ObjectBase.cpp#L661-L666)**: with the lock released, build the matching GUI. A graph-on-parent builds the GUI of every object inside its subpatch, which is the slow part the audio used to wait for.
   Python: `if kind is Kind.GRAPH: return GraphOnParent(ptr, parent)`, and the same for the other two.

### Commit 3: use the pure-data PR's table for top-level patches (3aa8c9796)

9. **[Libraries/pure-data](../../plugdata-prs/host-fixes/Libraries/pure-data)** isn't lines: plugdata records exactly which pure-data commit it builds with (a "submodule pointer", see below). This commit moves it from 267a42a8 (upstream's) to b7edbbb6 (the pure-data PR), so the table's functions exist.
   Python: like pinning `pure-data @ git+...@b7edbbb6` in requirements.txt.
10. **[Interface.h:773-788](../../plugdata-prs/host-fixes/Source/Pd/Interface.h#L773-L788)**: plugdata saves top-level patches with its own copy of Pd's save loop, and this gives it the same change as saveCanvas101.md's block 8. It builds the table (lines 774-775), looks up both ends of each cable (lines 778-779), and frees the table (line 788).
    Python: `index = build(canvas)`, then `src, dst = index[c.src], index[c.dst]` for each cable.

### Commit 4: don't save open subpatch tabs as extra patches (0395fdbba)

11. **[PluginProcessor.cpp:1257-1262](../../plugdata-prs/host-fixes/Source/PluginProcessor.cpp#L1257-L1262)**: while copying the list under the lock, keep only patches with no parent. `gl_owner` is the canvas a patch sits in, and a top-level patch has none. A subpatch open in a tab is already saved inside its parent, so it isn't saved again on its own. One thing changes: an open subpatch tab isn't reopened after loading, where before it came back as that wrong extra patch.
    Python: `patches_to_save = [p for p in self.patches if (c := p.raw_canvas()) and c.owner is None]`.

## A lock held for one block (blocks 3 and 7)

```cpp
    if (auto patch = ptr.get<t_canvas>()) {
        binbuf = pd::Interface::getCanvasBinbuf(patch.get());
    } else {
        return { };
    }
```

`ptr.get<t_canvas>()` takes Pd's lock and hands back a small object that holds it. When that object goes away, at the `}` closing the block it was made in, the lock is released. C++ calls this pattern RAII. The `if` is also false when the patch was already deleted; then the function returns an empty text (`return { };`).

`PluginProcessor.cpp` (block 1) uses the other way: `lockAudioThread();` and, later, `unlockAudioThread();`.

Python comparison: `with pd_lock:` for the first, and `pd_lock.acquire()` / `pd_lock.release()` for the second.

## The `<t_canvas>` in `get<t_canvas>()`: templates

Angle brackets choose a type for a function written once for many types. `get<t_canvas>()` means "give me this Pd object as a canvas"; `get<t_gobj>()` (block 7) means "as a plain object".

Python comparison: `typing.cast(Canvas, obj)`, but checked when compiling.

## Pointers that count their users (block 1)

`SmallArray<pd::Patch::Ptr, 16>` is plugdata's list type (room for 16 items before it needs more memory) holding `Patch::Ptr`s. A `Patch::Ptr` counts how many holders a patch has, and the patch is deleted when the last one lets go. That's how Python manages every object. Here it means the copy keeps each patch alive while it's saved, even if its tab is closed in the meantime.

## binbuf: Pd's list of words (blocks 3 to 5)

Pd keeps a patch's saved form as a "binbuf", a list of atoms: numbers and words, with `;` ending each line. `binbuf_gettext` writes them out as the text you see in a `.pd` file.

Python comparison: `["#X", "obj", 20, 60, "+", 1, ";"]`, turned into `"#X obj 20 60 + 1;"`.

## Returning two things through addresses (block 4)

Before:

```cpp
    static void getCanvasContent(t_canvas* cnv, char** buf, int* bufsize)
```

The caller passed the addresses of two variables (`&buf, &bufsize`), and the function wrote the text and its length into them: C's way to return two things. Now:

```cpp
    static t_binbuf* getCanvasBinbuf(t_canvas* cnv)
```

It returns one thing, the binbuf, and the caller makes the text.

Python comparison: `text, length = get_canvas_content(canvas)` became `words = get_canvas_binbuf(canvas)`.

## enum class, and falling into the next case (blocks 6 to 8)

`enum class Kind { Unknown, Array, Graph, Subpatch }` is a type that can only be one of those four names, like Python's `Enum`.

The code sits inside a `switch` over the object's class name, which ends:

```cpp
            if (kind == Kind::Subpatch)
                return new SubpatchObject(ptr, parent);
        }
        case hash("array define"):
            return new ArrayDefineObject(ptr, parent);
```

In C++ a `case` doesn't stop by itself: without a `return` (or `break`), the code carries straight on into the next `case`. If the object was deleted, `kind` stays `Unknown`, none of the three `return`s run, and the code falls into the next case, exactly as it did before this change.

Python comparison: `match` never falls through. The closest is a row of plain `if`s without `elif`.

## `if` with a declaration in it (block 11)

```cpp
        if (auto const* cnv = patch->getRawPointer(); cnv && !cnv->gl_owner)
            patchesToSave.add(patch);
```

Before the `;` is a variable made just for this `if` (C++17); after it, the test. `getRawPointer()` gives the patch's Pd canvas, or null if it's gone. `!cnv->gl_owner` is "it has no parent canvas".

Python comparison: `if (cnv := patch.raw_canvas()) and cnv.owner is None:`.

## A submodule pointer (block 9)

plugdata keeps pure-data as a git submodule. plugdata's own repository doesn't hold pure-data's files, only the hash of the pure-data commit to use. `git submodule update` then puts that commit into `Libraries/pure-data`. Commit 3 changes that hash, which is why the pure-data PR has to be merged first.

Blocks 1 to 3 and 7 to 8 are the actual fixes for the clicks: the lock is held only for the short parts. Block 10 makes what's still under the lock fast, block 11 is the separate subpatch-save fix, and blocks 4 to 6 and 9 are plumbing.
