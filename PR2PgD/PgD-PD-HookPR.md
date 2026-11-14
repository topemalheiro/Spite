# plugdata-pd PR: the trace hook for Detective Mode

- **Branch:** `pr/detective-mode-hook` on github.com/topemalheiro/plugdata-pd, on top of upstream `master` (267a42a8).
- **Files:** `src/s_inter.h` and `src/s_inter.c`, in `~/Projects/plugdata-prs/detective-mode-pd`.
- **Size:** 18 lines of code, plus comments. Nothing removed.

## 1. What it does, in one paragraph

When a Pd object sends a message out of an outlet, Pd calls `plugdata_forward_message()` once for each cable, just
before the next object gets the message. Today exactly one listener receives that call: plugdata's message dispatcher.
It keeps only the latest message per cable, which is right for a tooltip and wrong for a debugger. This PR adds a
second, optional listener slot. Detective Mode plugs into it while it's on, and gets every message in the order Pd
sends them. While nothing is plugged in, Pd behaves exactly as before.

## 2. The file names

Pd's source files start with a letter that says which part of Pd they belong to:

| prefix | part of Pd | examples |
|---|---|---|
| `m_` | **m**essage system: the core (atoms, symbols, classes, objects, how messages are sent) | `m_pd.h`, `m_obj.c`, `m_class.c` |
| `g_` | **g**raphical patch editor: canvases, boxes, the GUI objects | `g_canvas.c`, `g_editor.c` |
| `s_` | **s**ystem: everything that talks to the outside: audio and MIDI drivers, files, the GUI | `s_audio.c`, `s_midi.c`, `s_inter.c` |
| `d_` | **D**SP: the signal (`~`) objects | `d_osc.c`, `d_filter.c` |
| `x_` | control objects (`[trigger]`, `[list]`, maths) | `x_connective.c`, `x_list.c` |

- **`m_pd.h`** is Pd's main header: the one every external includes. It defines the basic types: `t_symbol`,
  `t_atom`, `t_pdinstance`. That's why F12 on those names takes you there.
- **`s_inter.c`**: "inter" is **interface**. The file's first comment says so: "Pd side of the Pd/Pd-gui interface".
  In vanilla Pd it sends drawing commands to the Tcl/Tk GUI. In plugdata, the GUI is plugdata itself, so this is
  where Pd talks to plugdata. `s_inter.h` is plugdata's own header for it (`register_gui_triggers()`,
  `plugdata_forward_message()`, and now `register_trace_trigger()`).

## 3. The C you need, in Python terms

| C | what it means | Python-ish |
|---|---|---|
| `int` | a whole number | `int` |
| `void` (as a return type) | returns nothing | `-> None` |
| `t_atom*`, `t_symbol*` | `*` makes it a **pointer**: the address of a value, not the value itself | a reference to an object |
| `void*` | a pointer to **anything**: an address whose type Pd doesn't know or care about | `Any` / `object` |
| `NULL` | the empty pointer | `None` |
| `if (ptr)` | true when the pointer isn't NULL | `if ptr is not None:` |
| `a && b` | and, short-circuit: `b` isn't even looked at when `a` is false | `a and b` |
| `struct { ... }` | a record with named fields | a dataclass |
| `a->b` | field `b` of the record that pointer `a` points to | `a.b` |
| `&x` | the address of `x`, to get a pointer to it | (no equivalent; passing `x` itself) |
| `typedef A B;` | gives type `A` the name `B` | `B = A` (a type alias) |
| `void (*name)(int, void*)` | a pointer to a function that takes an `int` and a `void*` and returns nothing | `Callable[[int, Any], None]` |
| `{ ... }` | a block. Braces make the scope; the indentation is only for people | indentation |
| `#if X ... #endif` | **preprocessor**: decided when compiling, not when running. The lines in between exist in the program only if `X` is true | a build setting |
| `#define NAME text` | a **macro**: the compiler replaces `NAME` with `text` everywhere | a text substitution |
| `// ...` and `/* ... */` | comments | `# ...` |

## 4. Line by line

### `s_inter.h:7-8`: the callback's type (your question)

```c
/* Same arguments as pd_message_callback: target, type, object or connection, selector, argc, argv */
typedef void (*pd_trace_callback)(void*, int, void*, t_symbol*, int, t_atom*);
```

> You asked: what are these, why 2 voids with pointers?

This line doesn't call anything. It only gives a name, `pd_trace_callback`, to "a pointer to a function that takes
these 6 arguments and returns nothing". In Python: `TraceCallback = Callable[[Any, int, Any, Symbol, int, list[Atom]], None]`.
It's the same shape as `pd_message_callback` on the line above, which plugdata already uses. The 6 arguments:

| # | type | what it is |
|---|---|---|
| 1 | `void*` | **target**: whatever plugdata registered, handed back unchanged. In plugdata it's the Detective Mode collector, the object that receives the messages. |
| 2 | `int` | **type**: 1 for a message through a cable, 0 for a message to an object (like an array's "redraw"). |
| 3 | `void*` | **the cable or object** the message is about. For type 1 it's the cable (a `t_outconnect*`, Pd's internal record of one connection). |
| 4 | `t_symbol*` | **the selector**, the message's name: `bang`, `float`, `list`, `set`... |
| 5 | `int` | **argc**: how many values the message has. |
| 6 | `t_atom*` | **argv**: the values themselves. Each atom is one float, symbol or pointer. |

**Why the two `void*`:**
- **The 1st one** is C's way of doing what Python does with a bound method. In Python you'd register
  `collector.enqueue`, and `self` comes along by itself. C has no bound methods, so you register the function and its
  `self` separately, and Pd calls `function(self, ...)`. `void*` because Pd doesn't know plugdata's C++ class and
  doesn't need to.
- **The 3rd one** is `void*` because `plugdata_forward_message()` is used for several kinds of things: cables,
  canvases, arrays. One "pointer to anything" covers them all.

**Symbols and atoms:**
- A **symbol** is Pd's version of an interned string: one shared object per distinct word, so `bang` is always the
  same address. Pd never frees symbols.
- **`argc` + `argv`** is the classic C way to pass a list. A C array doesn't know its own length, so the count travels
  next to it. In Python it would just be `args: list`.

### `s_inter.h:11-13`: the function that plugs a listener in

```c
/* Sets a second callback that gets every message plugdata_forward_message() forwards while debugging is on,
   in the order Pd sends them. A NULL target removes it. Call it with Pd's lock held, like Pd calls the callback */
void register_trace_trigger(t_pdinstance* instance, void* target, pd_trace_callback trace_callback);
```

This is the declaration: the name and arguments, so plugdata can call it. The body is in `s_inter.c`. The
arguments:
- **`instance`**: plugdata can run several separate Pd worlds in one program, for example one per plugin in a DAW,
  and each is a `t_pdinstance`. This picks which one.
- **`target`**: the `self` from above.
- **`trace_callback`**: the function.

To unplug, plugdata calls it with NULLs.

"With Pd's lock held": Pd runs under a lock (a mutex) that plugdata holds while Pd works. plugdata plugs in and
unplugs only while holding it, so Pd can never be halfway through calling the listener while plugdata swaps or
deletes it.

### `s_inter.c:180-181`: where Pd keeps the listener

```c
    pd_trace_callback trace_callback; // set by register_trace_trigger(), called by plugdata_forward_message()
    void *trace_target;               // passed back as trace_callback's first argument; NULL when there is none
```

These are two new fields in `struct _instanceinter`, the record that holds `s_inter.c`'s state, one per Pd
instance:
- **`trace_callback`** holds the function.
- **`trace_target`** holds its `self`.

They sit right under the three fields that already do the same for plugdata's existing listener (`gui_callback`,
`message_callback`, `callback_target`). The other things you saw in that struct, like the `pthread_mutex_t` fields,
are upstream's and unchanged.

### `s_inter.c:204-215`: `register_trace_trigger()` itself

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

- **It's a copy of the function right above it,** `register_gui_triggers()` at lines 192-202, which plugdata's
  maintainers wrote. Same layout, same blank line after `{`, same `#if`, so it reads as "the same thing, for the new
  listener". That's on purpose: reviewers like new code that looks like the code next to it.
- **`#if !PDINSTANCE ... #endif`:** Pd can be built with or without support for several instances. Built without,
  there's only the one global instance, `pd_maininstance`, so whatever `instance` was passed gets replaced by its
  address (`&`). This is decided when compiling, which is why those lines start at the left edge. Preprocessor lines
  conventionally aren't indented; the code between them is. The real scope is the `{ }` of the function.
- **The last two lines** store the function and its target in the chosen instance's record. In Python:
  `instance.pd_inter.trace_callback = trace_callback`.

### `s_inter.c:2523-2527`: the hook

```c
void plugdata_forward_message(int type, void* x, t_symbol *s, int argc, t_atom *argv)
{
    if(EXPECT_LIKELY(INTER->callback_target)) {
        INTER->message_callback(INTER->callback_target, type, (void*)x, s, argc, argv);
    }
    // Also hand the message to the trace callback, if one is set and debugging is on. Unlikely: plugdata only
    // registers one while it traces messages
    if(EXPECT_UNLIKELY(INTER->trace_target) && plugdata_debugging_enabled()) {
        INTER->trace_callback(INTER->trace_target, type, (void*)x, s, argc, argv); // same arguments as message_callback
    }
}
```

`plugdata_forward_message()` is plugdata's own function in Pd, the one Pd calls for every cable message. The first
`if` is upstream's: it gives the message to plugdata's existing listener. Ours, the second `if`, does the same for the
new one:

- **`INTER`** is a macro from line 75: `(pd_this->pd_inter)`. `pd_this` is the Pd instance that's running right now,
  so `INTER->trace_target` means "this instance's stored target".
- **`EXPECT_UNLIKELY(...)`** is a macro from lines 64-70. It tells the compiler "this is usually false", so it lays out
  the machine code for the common case. It changes nothing about what happens. The line above uses `EXPECT_LIKELY`
  because plugdata's main listener is almost always set. Ours is only set while Detective Mode is on.
- **`if (INTER->trace_target && plugdata_debugging_enabled())`:** if a listener is plugged in and plugdata's debugging
  flag is on. Thanks to `&&`, with no listener the flag isn't even read.
- **The call** runs the stored function with the stored target first, then the same five values the first `if`
  passes on. In Python: `inter.trace_callback(inter.trace_target, type, x, s, argc, argv)`.
- **`(void*)x`** is a cast, "treat `x` as a `void*`". `x` already is one, so it does nothing. It's there because the
  line above has it, and the two calls are kept identical.

## 5. Dangling pointers: you're right, there are none

A dangling pointer is an address of something that has been deleted. Each pointer here is safe for its own reason:

| pointer | why it can't dangle |
|---|---|
| `trace_target` (the collector) | plugdata unplugs (sets NULL) while holding Pd's lock, **before** deleting the collector. Pd only calls it with that lock held, so it never calls a deleted collector. |
| `x` (the cable) | plugdata only uses it as a name: a key to look the cable up, compared but never followed. Worst case, a cable is deleted and a new one gets the same address within a frame, and one message shows on the wrong cable. Cosmetic, never a crash. |
| `s` (the selector) | Pd never frees symbols while the instance lives. |
| `argv` (the values) | Valid only during the call. plugdata copies them (up to 32) into its queue before returning. |

The hook itself only stores pointers and passes them on: it never follows or frees any of them.

## 6. PR text to paste

**Title**

```
Add a trace hook for plugdata's Detective Mode
```

**Description**

```
Adds `register_trace_trigger()`, a second listener for the messages `plugdata_forward_message()` already forwards to plugdata, in the order Pd sends them.

**Why:** plugdata's existing message listener only needs the latest message per connection, so it coalesces them. A debugger needs every message, in order. Detective Mode (plugdata-team/plugdata#<N>) uses this hook to replay messages travelling along connections.

**What changes** (`s_inter.c`, `s_inter.h`; 18 lines of code, plus comments):
- `pd_trace_callback`, the same signature as `pd_message_callback`, and `register_trace_trigger(instance, target, callback)`, modelled on `register_gui_triggers()`. Passing NULL removes it.
- Two fields in `_instanceinter` to store the callback and its target.
- `plugdata_forward_message()` calls it after the existing callback, only if one is registered and `plugdata_debugging_enabled()` is set.

**Cost:** nothing changes for existing users. With no trace callback registered it is one pointer check, hinted as unlikely; plugdata only registers one while Detective Mode is on, and registers and removes it with Pd's lock held.

Tested together with the plugdata PR (Linux, plus a ThreadSanitizer run).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```
