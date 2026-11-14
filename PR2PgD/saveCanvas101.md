# saveCanvas101: saving a big canvas in linear time ([plugdata-pd#15](https://github.com/plugdata-team/plugdata-pd/pull/15))

**The links expect** `~/Projects/plugdata-prs/host-fixes-pd` to be checked out on the PR's branch `pr/save-canvas-linear-time` (it is).

## What this PR is, in plain words

**What you run into without it.** When plugdata saves a patch (Ctrl+S, autosave, or your DAW saving its session), Pd writes one line per cable: `#X connect 12 0 57 1;` means "object 12, outlet 0, goes to object 57, inlet 1". To get those two numbers, Pd counts from the first object until it reaches the one it wants, for both ends of every cable. With a few objects that's nothing. With thousands of objects and cables, it's millions of steps per save: about 150 ms for a subpatch with 8000 objects in my test. In plugdata, Pd's lock is held while it does this, and the audio waits for that lock, so you hear a click every time it saves.

**What the PR changes.** Before writing the cables, Pd makes a lookup table once: this object is number 0, that one is number 1, and so on. Each cable then looks its two numbers up straight away. The saved file is exactly the same, character for character. Three files: 78 lines added (the table, its comments, and its use in the save loop) and 2 replaced.

**What it needs first.** Nothing. It goes to plugdata-team/plugdata-pd, branch `master`. The plugdata stalls PR (hostStalls101.md) needs this one: it uses the same table for top-level patches, which plugdata saves with its own copy of this loop.

## How it came about

**Is this the click you heard in Spite?** It's one of them: the click on every save. The clicks you reported on 9 November, when switching tracks or changing a device's pins, came from something else: building a device's face while holding Pd's lock. plugdata's part of that fix is in PR 11 ([hostStalls101.md](hostStalls101.md), commit 2).

**Who ran into it, doing what.** You didn't report this one; an agent measured it. Later that day, the agent testing unlimited scenes ran a copy of your session with `SPITE_DSP_PROBE=1`, which logs the audio cycles that take too long. Each Ctrl+S made one cycle take 33 to 45 ms, when a cycle has 11.6 ms. That's an xrun: the sound drops out for a moment, and you hear a click. Saving a session with no plugins was clean, so the wait was in PgD4S. Spite asks every plugin for its state when it saves, and PgD4S held Pd's lock while it wrote out every open patch.

**What caused it there.** Your session had three converted Max for Live devices: Snake_Mod_Super, Flechtwerk and Oscilloscope. Converting a Max device can put thousands of objects and cables in one patch: Snake's saved form has 295,657 atoms (Pd's words and numbers). The objects agent timed Snake's save: 25.4 ms to collect it while holding the lock, then 16.6 ms to turn it into text. Most of the 25.4 ms was counting. For each cable, and for each of PgD4S's own presentation and attribute lines, Pd counted along Snake's objects to find an object's number (see "Why the old way was slow" below).

**The first fix, in PgD4S.** The objects agent fixed it the same day (plugdata commit ca37ababb, pure-data commit a16ec3ee):

- the text is made after letting go of the lock (upstream, that's PR 11's commit 1);
- the counting became this lookup table, and collecting Snake went from 25.4 ms to about 6 ms.

After both, saving the same session made no xrun.

**From PgD4S to this PR.** Presentation and attribute lines exist only in PgD4S, so this PR keeps only the cables part, in `canvas_saveto`. To show the problem without Spite or Max devices, I saved test patches in a small test host. A subpatch with 2,000 objects took 19 ms to save, and one with 8,000 took 146 ms, with the audio waiting the whole time. Four times the objects took almost eight times as long, because the counting grows with the square of the patch's size.

## The code, in reading order

1. **[g_canvas.h:567-574](../../plugdata-prs/host-fixes-pd/src/g_canvas.h#L567-L574)** describes the lookup table, with a comment on each field: a list of objects (`ci_keys`), a list of their numbers (`ci_values`), how many slots each list has (`ci_size`), and how many objects the canvas has (`ci_count`). See "A struct, and what the stars mean" below.
   Python: `@dataclass class CanvasIndex: keys: list; values: list; size: int; count: int`.
2. **[g_canvas.h:576-578](../../plugdata-prs/host-fixes-pd/src/g_canvas.h#L576-L578)** announces the three functions that use the table: build it, look an object up, and free it. These are only their first lines, so that other files (plugdata's among them) can call them. Their code is in blocks 4 to 6 (see "Announced in one file, written in another").
   Python: three `def` lines in a stub file, such as `def canvasindex_build(ci, canvas) -> None: ...`.
3. **[g_canvas.c:397-410](../../plugdata-prs/host-fixes-pd/src/g_canvas.c#L397-L410)** starts with a comment on what the whole table is for (lines 397-399). Then `canvasindex_hash` turns an object's memory address into a number that picks its first slot in the table, with a comment on how (lines 401-404). It drops the address's last 4 bits, which are always zero, mixes in the high bits, and multiplies by a big fixed number, so objects that sit next to each other in memory land far apart (see "The hash and its big number"). `static` means only this file can call it.
   Python: `def _hash(obj): p = id(obj) >> 4; p ^= p >> 17; return (p * 2654435761) & 0xFFFFFFFF`.
4. **[g_canvas.c:412-437](../../plugdata-prs/host-fixes-pd/src/g_canvas.c#L412-L437)** builds the table.

   * Lines 417-418 count the canvas's objects.
   * Lines 419-424 pick the table's size, as the comment on lines 419-421 says: a power of two (16, 32, 64...) at least twice the number of objects. The table holds only objects, one slot each, not inlets or cables. The extra room keeps at least half the slots empty, so a free one is always close by. It also has to have empty slots: a lookup for an object that isn't there only stops at an empty slot, so in a full table it would never stop. `mask` is the size minus one (see "Why a power of two").
   * Lines 425-426 store the size and the number of objects in the table.
   * Lines 427-428 get memory for the two lists, already filled with zeros, so every slot starts empty (see "Memory you ask for, label and free").
   * Lines 429-436 go through the objects again, counting `i` from 0. Each object goes into its first slot, or the next free one if that slot is taken (the comment on line 431), and `i` goes into the same slot of the other list.

   Python: `table = {obj: i for i, obj in enumerate(canvas.objects)}`, written out by hand (see "A hand-made dictionary").
5. **[g_canvas.c:439-448](../../plugdata-prs/host-fixes-pd/src/g_canvas.c#L439-L448)** looks an object up, as the comment on lines 439-440 says: start at its first slot and step forward until it's found, then return its number. Reaching an empty slot means the object isn't in this canvas. It then returns the number of objects, exactly what the old counting function returns in that case, so the saved file can't tell the difference.
   Python: `table.get(obj, len(canvas.objects))`.
6. **[g_canvas.c:450-454](../../plugdata-prs/host-fixes-pd/src/g_canvas.c#L450-L454)** gives the two lists' memory back. C doesn't do that for you (see "Memory you ask for, label and free").
   Python: nothing to write, because Python frees the dict once nothing uses it. Closest: `del table`.
7. **[g_readwrite.c:740](../../plugdata-prs/host-fixes-pd/src/g_readwrite.c#L740)** makes room for one table inside `canvas_saveto`, the function that saves a canvas.
   Python: `index = CanvasIndex()` at the top of the function.
8. **[g_readwrite.c:774-791](../../plugdata-prs/host-fixes-pd/src/g_readwrite.c#L774-L791)** is where saving uses the table.

   * Line 775 builds it once, right before the cables are written.
   * Lines 776-790 are the existing loop over every cable. Only lines 779-780 changed: the two object numbers now come from the table (`canvasindex_get`) instead of from counting (`canvas_getindex`).
   * Lines 783-784, unchanged, write one cable's line: `binbuf_addv(b, "ssiiii;", ...)`. The `"ssiiii;"` says what follows, one letter per value: `s` a symbol (a word), `i` an integer, `;` the end of the line. So it's two words and four numbers: `#X connect 12 0 57 1;`. C can't tell what kinds of values arrive in a `...` list, so you spell them out, as with `printf("%s %d", ...)`. Lines 787-788 add one more word (`"ssiiiis;"`) for a cable whose shape plugdata saves too.
   * Line 791 frees the table.
   * `canvas_saveto` saves every subpatch, and Pd also uses it when you copy, or undo, a selection with a subpatch in it, so those get faster too.

   Python: `index = build(canvas)`, then `for c in cables: out.append(f"#X connect {index[c.src]} {c.outlet} {index[c.dst]} {c.inlet};")`.

## Why the old way was slow: counting along a chain

Pd keeps a canvas's objects as a chain: the canvas points to the first object (`gl_list`), and each object points to the next one (`g_next`). There's no "give me item 57" and no "which number is this one": to find an object's number, you walk the chain and count. That's the old function, unchanged, at [g_canvas.c:388-395](../../plugdata-prs/host-fixes-pd/src/g_canvas.c#L388-L395):

```c
int canvas_getindex(t_canvas *x, t_gobj *y)
{
    t_gobj *y2;
    int indexno;
    for (indexno = 0, y2 = x->gl_list; y2 && y2 != y; y2 = y2->g_next)
        indexno++;
    return (indexno);
}
```

In Python that's `canvas.objects.index(obj)`: it looks at every item until it finds the right one. Saving called it twice per cable. With 8000 objects and 15,000 cables that's about 15,000 × 2 × 4,000, 120 million steps, per save. With the table it's 30,000 lookups of a step or two each.

## A struct, and what the stars mean (block 1)

A `struct` is a record with named fields, like a Python dataclass without methods. `typedef ... t_canvasindex;` gives it a short name:

```c
typedef struct _canvasindex
{
    t_gobj **ci_keys;       /* the objects, each in its slot; 0 if free */
    int *ci_values;         /* each object's index, in the same slot */
    unsigned int ci_size;   /* number of slots, a power of two */
    int ci_count;           /* number of objects in the canvas */
} t_canvasindex;
```

A `*` means "the memory address of". `int *ci_values` is the address of the first of many ints: that's how C makes a list, as a block of memory plus where it starts. `t_gobj **ci_keys` has two stars: the start of a list whose items are addresses of objects. So `ci_keys` is a list of objects, and `ci_values` a list of numbers.

A function's parameters are written the same way: each one is the type, then the name, and commas separate them. `canvasindex_build(t_canvasindex *ci, t_canvas *x)` has two: `ci`, of type `t_canvasindex *` (the table's address), and `x`, of type `t_canvas *` (the canvas's address). The star belongs to the type, not to a third parameter, and the space around it doesn't matter: `t_canvasindex *ci`, `t_canvasindex* ci` and `t_canvasindex * ci` are the same. Pd puts the star by the name; plugdata's C++ puts it by the type (`t_canvas* cnv`). Python puts the name first: `def canvasindex_build(ci: CanvasIndex, x: Canvas):`.

Two more symbols you'll see:

- `ci->ci_size` reads a field through an address: `ci` isn't the table itself, it's where the table is. Python: `ci.ci_size`. (Python's own `->`, in `def f() -> int:`, is something else: a note on what the function returns.)
- `&index` (as in `canvasindex_build(&index, x)`) passes the table's address, so the function fills in the caller's own table rather than a copy. Python passes objects this way without saying so.

## A hand-made dictionary (blocks 3 to 5)

C has no built-in dict, so the PR makes a small one:

- Every object gets a slot number from its address (block 3), cut down to the table's size.
- If that slot is taken, the object goes into the next one, or the one after, until a free slot turns up. This is called linear probing.
- To look an object up, start at the same slot and step forward the same way until you find it. Reaching an empty slot means it isn't there.

Building, at [g_canvas.c:431-435](../../plugdata-prs/host-fixes-pd/src/g_canvas.c#L431-L435):

```c
            /* from the object's first slot, step to the first free one */
        for (h = canvasindex_hash(y) & mask; ci->ci_keys[h]; h = (h + 1) & mask)
            ;
        ci->ci_keys[h] = y;
        ci->ci_values[h] = i;
```

The `for` line does all the stepping. Its body is just `;`, an empty statement, so the loop does nothing but stop at the first empty slot. The `;` sits on its own line, indented, to show the empty body is on purpose: a stray `;` at the end of a `for` line is a classic C bug. The two lines after it run once, when the loop has stopped. In Python, the stepping goes in the body:

```python
h = _hash(obj) & mask
while keys[h] is not None:
    h = (h + 1) & mask
keys[h] = obj
values[h] = i
```

The table is at most half full, so a lookup usually finds its object in the first or second slot it tries: 1.1 to 1.3 steps on average when I simulated it with Pd-like objects, at most 7. That stays the same however big the patch gets, where counting took thousands of steps per cable. Python's own `dict` is built on the same idea, and grows once it's about two-thirds full.

## The hash and its big number (block 3)

```c
static unsigned int canvasindex_hash(t_gobj *y)
{
    size_t p = (size_t)y >> 4;
    p ^= p >> 17;
    return (unsigned int)(p * 2654435761u);
}
```

- `(size_t)y` turns the object's address into a plain number. Every object has its own, so it works as an ID. Python: `id(obj)`.
- `>> 4` drops the number's last 4 bits. Memory is handed out aligned, so an address always ends in zero bits. Left in, they'd crowd the objects into a few of the slots, because multiplying never moves bits down.
- `p ^= p >> 17` mixes the high bits into the low ones: `>>` shifts right, `^` is xor.
- `* 2654435761u` scrambles the number, so objects that sit next to each other in memory land far apart. The number is 2³² divided by the golden ratio (2,654,435,769.5), rounded down to a prime. Multiples of the golden ratio spread out evenly instead of bunching up; Donald Knuth wrote this up as "Fibonacci hashing".
- The `u` makes the number unsigned, with no minus sign. The product is far bigger than 32 bits, and with unsigned numbers C just drops what doesn't fit. `(unsigned int)(...)` keeps the bottom 32 bits.

```python
def canvasindex_hash(obj):
    p = id(obj) >> 4
    p ^= p >> 17
    return (p * 2654435761) & 0xFFFFFFFF   # Python's ints never overflow, so cut to 32 bits by hand
```

## Why a power of two (`& mask`)

A slot number has to stay inside the table, which in Python you'd do with `h % size`. When `size` is a power of two, `h & (size - 1)` gives the same answer and is faster. That's why line 423 doubles the size (`size <<= 1` is `size *= 2`) until it's big enough, and line 424 keeps `mask = size - 1`. `(h + 1) & mask` steps to the next slot, and from the last slot wraps back to slot 0.

## Memory you ask for, label and free (blocks 4 and 6)

Python frees an object once nothing uses it. C doesn't: memory you ask for stays yours until you give it back. `getbytes` is Pd's way to ask for memory (it also fills it with zeros) and `freebytes` gives it back. That's why `canvas_saveto` calls `canvasindex_free` after its loop. Without it, every save would leak the table's memory.

`getbytes` hands back raw memory: an address with no type, just bytes. The type in parentheses in front of it is a cast, "treat this as...", at [g_canvas.c:427-428](../../plugdata-prs/host-fixes-pd/src/g_canvas.c#L427-L428):

```c
    ci->ci_keys = (t_gobj **)getbytes(size * sizeof(t_gobj *));
    ci->ci_values = (int *)getbytes(size * sizeof(int));
```

The first labels its memory "a list of object addresses" (two stars), the second "a list of ints". `sizeof(t_gobj *)` is the size of one address, 8 bytes, times `size` slots. Python: `keys = [None] * size`. Python builds the list for you; C hands you bytes, and you say what they are.

## Announced in one file, written in another (block 2)

It's the same split pdHook101.md explains in "One function, written twice". `g_canvas.h` only announces the three functions (block 2), and `g_canvas.c` has their code (blocks 4 to 6). plugdata's `Interface.h` calls them knowing only `g_canvas.h`. `EXTERN` in front of each announcement is Pd's own word for C's `extern` ("this lives in some other file"), plus a note for Windows builds about making it callable from outside the Pd library.

Block 8 is the actual fix: saving looks the numbers up instead of counting. Blocks 1 to 7 are the table it looks them up in.
