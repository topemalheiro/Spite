# pdHook101: the trace hook for Detective Mode (plugdata-pd PR)

**The links expect** `~/Projects/plugdata-prs/detective-mode-pd` to be checked out on `pr/detective-mode-hook` (it is). This is PR 1 in [order.md](order.md).

## What this PR is, in plain words

**What you run into without it.** Nothing by itself: this PR only prepares Pd for Detective Mode (PR 13, see [detectiveMode101.md](detectiveMode101.md)). Without it, Detective Mode can't show every message in the order Pd sends them.

**Why.** plugdata's Pd already tells plugdata about the messages on cables, through one callback. That callback feeds a part of plugdata made for refreshing the screen, and it keeps only the newest message per cable. When one cable carries 1, 2, 3 in a row, plugdata only gets the 3, and the order across cables is lost too.

**What the PR changes.** Pd gets a second, optional callback. While plugdata has one plugged in, Pd hands it every cable message at the moment the message is sent. It adds 25 lines to 2 files (15 of code, 7 of comments, 3 blank) and removes nothing. With nothing plugged in, the cost is one check per message.

**What it needs first.** Nothing. PR 13 needs this one.

## The code, in reading order

1. **[s_inter.h:7-8](../../plugdata-prs/detective-mode-pd/src/s_inter.h#L7-L8)** describes the function plugdata hands to Pd. For each message, Pd passes it six things: who to tell, the kind of message, which cable, the message name, how many values there are, and the values. It takes the same arguments as the message callback that's already there.
   Python: a type hint, `TraceCallback = Callable[[target, kind, cable, name, count, values], None]`.
2. **[s_inter.h:11-13](../../plugdata-prs/detective-mode-pd/src/s_inter.h#L11-L13)** announces the function that plugs that callback in, so plugdata can call it. Passing NULL unplugs it. It's only the function's first line, ending in `;`, with no code: its code is block 4 (see "One function, written twice" below).
   Python: a `def` line with no body, like in a stub file.
3. **[s_inter.c:180-181](../../plugdata-prs/detective-mode-pd/src/s_inter.c#L180-L181)** adds two empty slots to Pd's per-instance data: the function to call, and the object to call it on (plugdata's trace collector).
   Python: `self.trace_callback = None` and `self.trace_target = None`.
4. **[s_inter.c:204-215](../../plugdata-prs/detective-mode-pd/src/s_inter.c#L204-L215)** is the same function as block 2, this time with its code. Block 2 only announces it; this is the part that runs when plugdata calls it. It fills those two slots. plugdata calls it with its collector when Detective Mode turns on, and with NULL when it turns off. The `#if` part only matters for Pd builds that run a single Pd.
   Python: `def register_trace_trigger(self, target, callback): self.trace_callback = callback; self.trace_target = target`.
5. **[s_inter.c:2523-2527](../../plugdata-prs/detective-mode-pd/src/s_inter.c#L2523-L2527)** is the hook itself.

   * Every time a message goes down a cable, Pd already tells plugdata through the normal callback.
   * Now, if a collector is plugged in, Pd also tells the collector straight away, in the exact order the messages happen.
   * The normal callback can't do this job: plugdata keeps only the newest message per cable, so the in-between messages and their order are lost.

   Python: `if self.trace_target and debugging_enabled(): self.trace_callback(self.trace_target, kind, cable, name, count, values)`.

Block 5 is the reason the Pd PR exists. Blocks 1 to 4 are what let plugdata plug into it.

## One function, written twice (blocks 2 and 4)

In C, a function that other files use is usually written in two places.

**The declaration, in the header (`.h`).** Only the function's first line: its name and what it takes, ending in `;`. Block 2 is this (line 13; lines 11-12 are its comment):

```c
void register_trace_trigger(t_pdinstance* instance, void* target, pd_trace_callback trace_callback);
```

**The definition, in the `.c` file.** The same first line again, without the `;`, then the code between `{` and `}`. That code is called the function's body. Block 4 is this:

```c
// Like register_gui_triggers(), for the trace callback
void register_trace_trigger(t_pdinstance* instance, void* target, pd_trace_callback trace_callback)
{

#if !PDINSTANCE
    instance = &pd_maininstance; // single-instance build: only the main instance exists
#endif

    // A NULL target removes the callback: plugdata_forward_message() only calls it while trace_target is set
    instance->pd_inter->trace_callback = trace_callback;
    instance->pd_inter->trace_target = target;
}
```

C needs both because it compiles each file on its own. plugdata's `DetectiveMode.cpp` never sees `s_inter.c`: it only includes `s_inter.h`, and the declaration there tells it that the function exists and what to pass it. At the end of the build, the linker connects plugdata's call to the code in `s_inter.c`.

Python doesn't split a function like this: a `def` always comes with its code. The closest thing is a stub file (`.pyi`), where `def register_trace_trigger(instance, target, trace_callback) -> None: ...` only describes the function, and the real code lives in the `.py` file.

## Questions that came up

### Shouldn't block 5 check "Detective Mode is on" instead of "debugging is on"?

It does check Detective Mode. The `if` in block 5 has two checks:

- **`INTER->trace_target`**: plugdata fills this in when Detective Mode turns on ([DetectiveMode.cpp:100](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L100)) and clears it when it turns off ([DetectiveMode.cpp:108](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L108)). So this check is the "Detective Mode is on" test.
- **`plugdata_debugging_enabled()`**: Pd's existing switch for "report the messages on cables to plugdata". Its name can't say Detective Mode, because Pd's C code knows nothing about plugdata's features, and giving Pd its own Detective Mode switch would mean more lines in Pd. plugdata turns this switch on while Detective Mode is on ([DetectiveMode.cpp:81](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L81)). It's also on for plugdata's older "Debug connections" option, which is on by default. In plugin mode plugdata turns it off, so the trace stops there.

### When does one cable carry many messages at once?

Often: any time a patch has a "for loop".

- **Loops:** `[until]` in Pd, or `[uzi 10]` in Max, sends a burst of bangs out of the same outlet.
- **Splitting a list:** `[iter]` (Max, and cyclone in plugdata) turns `[1 2 3(` into 1, then 2, then 3, all down the same cable.
- **Dumps:** `[coll]` with "dump", or `[qlist]` and `[text sequence]`, sends many lines at once.
- **MIDI:** a chord into `[notein]` sends several notes in a row. `[midiin]` sends a single note-on as 3 separate bytes.
- **Scripts:** in Max's `[js]`, `for (i = 0; i < 10; i++) outlet(0, i);` sends 10 messages down one cable straight away. Lua in plugdata (pdlua) does the same.

The order they run in, in both Pd and Max:

1. Each message goes all the way to the end of its chain first. Only after that does the object send its next message, or fire its next outlet.
2. An object with several outlets fires them right to left. `[t b b]` and `[unpack]` always start from the rightmost outlet.
3. One outlet with several cables is where the two differ. Max goes by where the receiving objects sit: rightmost first, and bottom first when they're stacked. Pd promises no order; in practice the cable you connected first goes first, which is why Pd patches use `[trigger]`.

An example: `[1 2 3(` goes into `[iter]`, which goes into `[t f f]`, which sends to `[B]` from its right outlet and to `[A]` from its left outlet. The order is 1 to B, 1 to A, then 2 to B, 2 to A, then 3 to B, 3 to A.

This is what block 5 is for. plugdata's normal callback keeps only the newest message per cable, so on the cable between `[iter]` and `[t f f]` it would only ever see the 3. The trace callback gets all six steps in that exact order.
