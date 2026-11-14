# Add a trace hook for plugdata's Detective Mode

[← Overview](README.md) · **PR:** [plugdata-team/plugdata-pd#14](https://github.com/plugdata-team/plugdata-pd/pull/14) · plugdata-pd `master` ← `topemalheiro:pr/detective-mode-hook` · 1 commit, +25 in `src/s_inter.c` and `src/s_inter.h` · needs nothing first · [plugdata-team/plugdata#2434](https://github.com/plugdata-team/plugdata/pull/2434) needs this one ([13-detective-mode.md](13-detective-mode.md))

## What it does

**What you run into without it.** Nothing by itself: this PR only prepares Pd for Detective Mode. Without it, Detective Mode can't show every message in the order Pd sends them.

**Why.** Pd already tells plugdata about the messages on connections, through `plugdata_forward_message()` and the message callback. That callback feeds the part of plugdata that refreshes the GUI, which only needs the newest message per connection. When one connection carries 1, 2, 3 in a row, only the 3 is kept, and the order across connections is lost too. A debugger needs every message, in order.

**What changes.** Pd gets a second, optional callback. While plugdata has one registered, Pd hands it every connection message at the moment the message is sent. It adds 25 lines to 2 files (15 of code, the rest comments and blank lines) and removes nothing. With nothing registered, the cost is one pointer check per forwarded message.

## The code, in reading order

1. **[s_inter.h:7-8](https://github.com/topemalheiro/plugdata-pd/blob/caeb13596f27ef87b5037afaa5fdd1145e1d252a/src/s_inter.h#L7-L8)** is the callback's type, `pd_trace_callback`. It takes the same arguments as `pd_message_callback`: target, type, object or connection, selector, argc, argv.
2. **[s_inter.h:11-13](https://github.com/topemalheiro/plugdata-pd/blob/caeb13596f27ef87b5037afaa5fdd1145e1d252a/src/s_inter.h#L11-L13)** declares `register_trace_trigger(instance, target, callback)`. A NULL target removes the callback. The comment says to call it with Pd's lock held.
3. **[s_inter.c:180-181](https://github.com/topemalheiro/plugdata-pd/blob/caeb13596f27ef87b5037afaa5fdd1145e1d252a/src/s_inter.c#L180-L181)** adds two fields to `_instanceinter`: the callback, and the target passed back as its first argument (plugdata's trace collector).
4. **[s_inter.c:204-215](https://github.com/topemalheiro/plugdata-pd/blob/caeb13596f27ef87b5037afaa5fdd1145e1d252a/src/s_inter.c#L204-L215)** is `register_trace_trigger()`, modelled on `register_gui_triggers()` right above it, including the `#if !PDINSTANCE` part. It fills those two fields. plugdata calls it with its collector when Detective Mode turns on, and with NULL when it turns off.
5. **[s_inter.c:2523-2527](https://github.com/topemalheiro/plugdata-pd/blob/caeb13596f27ef87b5037afaa5fdd1145e1d252a/src/s_inter.c#L2523-L2527)** is the hook itself, at the end of `plugdata_forward_message()`. After the existing callback, if a trace target is registered and `plugdata_debugging_enabled()` is set, it calls the trace callback with the same arguments. The check is hinted as unlikely (`EXPECT_UNLIKELY`), since plugdata only registers a target while it traces.

Block 5 is the reason this PR exists. Blocks 1 to 4 are what let plugdata register for it.

## Why the hook checks two things

The `if` in block 5 has two checks:

- **`INTER->trace_target`**: plugdata sets this when Detective Mode turns on ([DetectiveMode.cpp:100](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L100)) and clears it when it turns off ([DetectiveMode.cpp:108](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L108)). So this is the "Detective Mode is on" test.
- **`plugdata_debugging_enabled()`**: Pd's existing switch for forwarding connection messages to plugdata at all. plugdata keeps it on while Detective Mode is on ([DetectiveMode.cpp:81](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L81)), as well as for the existing connection-debugging option. In plugin mode plugdata turns it off, so the trace stops there too, without a separate switch in Pd.

How it was tested, and what wasn't, is in the PR description.
