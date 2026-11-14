# canvas_saveto(): look up the objects' indices in a table

[← Overview](README.md) · **PR:** [plugdata-team/plugdata-pd#15](https://github.com/plugdata-team/plugdata-pd/pull/15) · plugdata-pd `master` ← `topemalheiro:pr/save-canvas-linear-time` · 1 commit, +78 −2 in 3 files · needs nothing first · [plugdata-team/plugdata#2432](https://github.com/plugdata-team/plugdata/pull/2432) needs this one ([11-audio-stalls-on-save.md](11-audio-stalls-on-save.md))

## What it does

**What you run into without it.** When a patch is saved (Ctrl+S, autosave, or a DAW saving its session), Pd writes one `#X connect` line per connection, and gets its two object numbers from `canvas_getindex()`, which counts from the first object until it reaches the one it wants. With thousands of objects and connections that's millions of steps per save: saving the plugin state with a `[pd sub]` holding 8000 objects and 15520 connections took 146-246 ms in my test. In plugdata, Pd's lock is held while it does this, and the audio thread waits for that lock, so the audio drops out on every save.

**What changes.** Before writing the connections, Pd builds a lookup table once: object → index. Each connection then looks its two numbers up in constant time. Measured inside plugdata, together with the plugdata PR that uses it, the same save takes 19-29 ms, and the saved state is the same, byte for byte.

## The code, in reading order

1. **[g_canvas.h:567-574](https://github.com/topemalheiro/plugdata-pd/blob/b7edbbb68f0d930f701f3f23cfbfeec90c82b9cd/src/g_canvas.h#L567-L574)** is the table, `t_canvasindex`, with a comment on each field: the objects (`ci_keys`), their indices (`ci_values`), the number of slots (`ci_size`), and the number of objects in the canvas (`ci_count`).
2. **[g_canvas.h:576-578](https://github.com/topemalheiro/plugdata-pd/blob/b7edbbb68f0d930f701f3f23cfbfeec90c82b9cd/src/g_canvas.h#L576-L578)** declares the three functions that use it: build, get and free. They're `EXTERN` so plugdata can call them too.
3. **[g_canvas.c:397-410](https://github.com/topemalheiro/plugdata-pd/blob/b7edbbb68f0d930f701f3f23cfbfeec90c82b9cd/src/g_canvas.c#L397-L410)** starts with a comment on what the table is for (lines 397-399). Then `canvasindex_hash` turns an object's address into the number that picks its first slot, with a comment on how (lines 401-404). It drops the address's last 4 bits, which are always zero because allocations are aligned, mixes in the high bits, and multiplies by 2654435761 (Knuth's multiplicative hash), so objects that sit next to each other in memory spread over the table.
4. **[g_canvas.c:412-437](https://github.com/topemalheiro/plugdata-pd/blob/b7edbbb68f0d930f701f3f23cfbfeec90c82b9cd/src/g_canvas.c#L412-L437)** builds the table.
   - Lines 417-418 count the canvas's objects.
   - Lines 419-424 pick the table's size, as the comment on lines 419-421 says: a power of two, at least twice the number of objects and at least 16. Half the slots stay empty, so a free one is always close by, and a lookup for an object that isn't there always reaches an empty slot and stops. `mask` is the size minus one.
   - Lines 425-426 store the size and the number of objects.
   - Lines 427-428 allocate the two arrays with `getbytes()`, which zeroes them, so every slot starts empty.
   - Lines 429-436 go through the objects again, counting `i` from 0. Each object goes into its first slot, or the next free one if that slot is taken (linear probing), and `i` goes into the same slot of the other array.
5. **[g_canvas.c:439-448](https://github.com/topemalheiro/plugdata-pd/blob/b7edbbb68f0d930f701f3f23cfbfeec90c82b9cd/src/g_canvas.c#L439-L448)** looks an object up: start at its first slot and step forward until it's found, then return its index. Reaching an empty slot means the object isn't in this canvas. It then returns the number of objects, exactly what `canvas_getindex()` returns in that case, so the saved text can't differ.
6. **[g_canvas.c:450-454](https://github.com/topemalheiro/plugdata-pd/blob/b7edbbb68f0d930f701f3f23cfbfeec90c82b9cd/src/g_canvas.c#L450-L454)** frees the two arrays.
7. **[g_readwrite.c:740](https://github.com/topemalheiro/plugdata-pd/blob/b7edbbb68f0d930f701f3f23cfbfeec90c82b9cd/src/g_readwrite.c#L740)** declares one table in `canvas_saveto`.
8. **[g_readwrite.c:774-791](https://github.com/topemalheiro/plugdata-pd/blob/b7edbbb68f0d930f701f3f23cfbfeec90c82b9cd/src/g_readwrite.c#L774-L791)** is where saving uses the table.
   - Line 775 builds it once, right before the connections are written.
   - Lines 776-790 are the existing loop over every connection. Only lines 779-780 changed: the two object numbers now come from `canvasindex_get` instead of `canvas_getindex`.
   - Line 791 frees the table.

Block 8 is the actual fix. Blocks 1 to 6 are the table it uses, and block 7 is its declaration.

## Why the old way was slow

`canvas_getindex()` walks the canvas's linked list from `gl_list` and counts until it finds the object ([g_canvas.c:388-395](https://github.com/topemalheiro/plugdata-pd/blob/b7edbbb68f0d930f701f3f23cfbfeec90c82b9cd/src/g_canvas.c#L388-L395), unchanged). Saving called it twice per connection. With 8000 objects and 15,000 connections that's about 15,000 × 2 × 4,000, 120 million steps, per save. With the table it's 30,000 lookups of a step or two each.

`canvas_saveto()` saves every subpatch, and Pd also uses it when a selection with a subpatch in it is copied or undone, so those get faster too. Top-level patches are saved by plugdata's own copy of this loop, which [plugdata-team/plugdata#2432](https://github.com/plugdata-team/plugdata/pull/2432) changes in the same way.

How it was tested, and what wasn't, is in the PR description.
