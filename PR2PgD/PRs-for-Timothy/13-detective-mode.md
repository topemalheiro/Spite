# Detective Mode: watch messages travel along connections in execution order

[← Overview](README.md) · **PR:** [plugdata-team/plugdata#2434](https://github.com/plugdata-team/plugdata/pull/2434) · plugdata `develop` ← `topemalheiro:pr/detective-mode` · 3 commits, +902 −10 in 20 files (one is the pure-data pointer) · needs [plugdata-team/plugdata-pd#14](https://github.com/plugdata-team/plugdata-pd/pull/14) first ([01-pd-trace-hook.md](01-pd-trace-hook.md))

## What it does

**What you run into without it.** When a patch does the wrong thing, you can't see what happened in which order. plugdata can already show a connection's latest message when you hover over it, and flash objects when they get a message. But Pd runs a whole chain of messages in one instant, in an order that depends on outlet positions and on how the patch is wired. That order is where most patching bugs come from, and today you work it out in your head or with `[print]` objects.

**What changes.** The status bar gets a "Detective" toggle. While it's on, every message Pd sends through a connection shows up as a dot travelling along that connection, with the message's text in a bubble next to it. They play one at a time, in the exact order Pd sent them. A slider next to the toggle sets the speed. A new sidebar panel can pause, step through one message at a time, and list the last 64 messages of the connection you select. It's off by default.

**How it works, in three steps.**

1. Pd writes every connection message into a queue, at the moment it sends it. This uses the hook from the pure-data PR.
2. 60 times a second, plugdata empties that queue into a pending list, and plays the pending list one message at a time.
3. The canvas draws the dot and the bubble. The status bar and the sidebar panel are the controls.

**How big it is.** 3 commits, +902 −10. Five new files hold 797 of those lines. The rest are small additions to 14 existing files. Nothing is specific to one operating system.

**What it needs first.** [plugdata-team/plugdata-pd#14](https://github.com/plugdata-team/plugdata-pd/pull/14), the trace hook in plugdata-pd. Commit 1 of this PR points `Libraries/pure-data` at that PR's commit on my fork, and has to be changed to the merged commit before this one is merged.

## The code, in reading order

### Commit 1: use the Pd that has the hook

1. **[Libraries/pure-data](https://github.com/topemalheiro/plugdata-pd/commit/caeb13596f27ef87b5037afaa5fdd1145e1d252a)**: the submodule pointer moves from 267a42a8 (upstream's) to caeb1359 (the pure-data PR's commit).

### Commit 2: Detective Mode

#### A. Recording the messages inside Pd

All of this is one new file, `ConnectionTraceCollector.h`.

2. **[ConnectionTraceCollector.h:14-34](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Pd/ConnectionTraceCollector.h#L14-L34)** is one recorded message: which connection, the selector, its sequence number, and a copy of up to 32 of its atoms. It copies the atoms because `argv` is only valid during the call. `truncated` says there were more than 32.
3. **[ConnectionTraceCollector.h:36-45](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Pd/ConnectionTraceCollector.h#L36-L45)** starts the collector, the object that holds the queue. It remembers the symbol `_activity`: activity notifications are forwarded the same way as connection messages, so block 4 needs it to skip them.
4. **[ConnectionTraceCollector.h:47-56](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Pd/ConnectionTraceCollector.h#L47-L56)** is `enqueue`, the function registered with `register_trace_trigger()`. Pd calls it for every forwarded message, with its lock held.
   - It ignores anything that isn't a connection message (`type != 1`) and the `_activity` notifications.
   - It numbers the message and puts it in the queue with `try_emplace`. If the queue is full, the message is dropped: Pd must never wait.
5. **[ConnectionTraceCollector.h:58-70](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Pd/ConnectionTraceCollector.h#L58-L70)** is `dequeue`, the message thread's side. It hands the recorded messages to a callback, oldest first, at most 4096 per call.
6. **[ConnectionTraceCollector.h:72-77](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Pd/ConnectionTraceCollector.h#L72-L77)** is the collector's data: a `moodycamel::ReaderWriterQueue` with room for 4096 messages, the `_activity` symbol, and the sequence counter, which is only touched with Pd's lock held.

#### B. Turning it on and off

7. **[DetectiveMode.h:23-59](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.h#L23-L59)** is the public interface, under a comment on the threading: is it on, pause, step, render, a connection's history, and `Message` (what one shown message holds).
8. **[DetectiveMode.cpp:62-76](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L62-L76)** is the constructor and destructor. Constructed: it ties its speed to the `detective_speed` setting, and turns on if the `detective_mode` setting says so. Destroyed: it unregisters from Pd, under the lock.
9. **[DetectiveMode.cpp:90-122](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L90-L122)** is `setEnabled`.
   - **On:** it locks the audio thread, creates the collector, registers it with `register_trace_trigger`, and unlocks. Then it starts a 60 Hz timer (block 15).
   - **Off:** it stops the timer, locks, unregisters, unlocks, deletes the collector and forgets every message.
   - Either way, it then updates Pd's debugging switch (block 10).
10. **[DetectiveMode.cpp:78-88](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L78-L88)** has two small functions. `updatePdDebugging` sets `set_plugdata_debugging_enabled`: on if the `debug_connections` setting or Detective Mode is on. `settingsChanged` turns Detective Mode on or off when the `detective_mode` setting changes.
11. **[OverlayDisplaySettings.h:162](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Dialogs/OverlayDisplaySettings.h#L162)** and **[PluginMode.h:156](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginMode.h#L156)** are the two places that used to set Pd's debugging switch from `debug_connections` alone. They now call `updatePdDebugging`, so switching that option off, or leaving plugin mode, doesn't cut off Detective Mode's messages. The other side of that is **[Connection.cpp:578-579](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Connection.cpp#L578-L579)**: the hover tooltip on a connection was shown whenever Pd's debugging switch was on, so it now also checks the `debug_connections` setting itself. Turning Detective Mode on doesn't bring the tooltips back when "Debug" is off.
12. **[PluginProcessor.cpp:194-195](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginProcessor.cpp#L194-L195)** creates the one Detective Mode each instance has, and **[PluginProcessor.cpp:207-209](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginProcessor.cpp#L207-L209)** deletes it first in the destructor, so Pd stops recording before the patches are deleted.
13. **[SettingsFile.h:152-153](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Utility/SettingsFile.h#L152-L153)** adds the two settings with their defaults: off, and speed 1.

#### C. Playing the messages

14. **[DetectiveMode.h:61-118](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.h#L61-L118)** is the private state.
    - `Bubble`: what's drawn for the message in one canvas (its connection, its text layout, where the bubble and dot are).
    - `pending`: the messages waiting to be shown. `history`: the last messages per connection. `current`: the message being shown. `progress`: how far along the connection it is, from 0 to 1.
    - `paused`, `stepping`, `speed`.
    - The limits: 400 ms per connection at speed 1, 4096 pending messages, 64 messages of history per connection, 300 characters of text in a bubble.
15. **[DetectiveMode.cpp:263-297](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L263-L297)** is the timer callback, 60 times a second. This is where the playing happens.
    - It collects the new messages (block 16).
    - If a message is showing, it moves it forward: the time since the last tick, times the speed, divided by 400 ms. Paused means it doesn't move. A long stall counts as 50 ms at most, so the message carries on from where it was.
    - It ends the message when it reaches the end of the connection, when the connection was deleted, or when the connection can't be seen while playing.
    - If nothing is showing, it starts the next one (block 18). Then it updates the bubbles (block 22).
16. **[DetectiveMode.cpp:191-213](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L191-L213)** drains the collector's queue. For each recorded message: skip it if its patch isn't open, turn it into a `Message`, add it to its connection's history (dropping the oldest above 64), and add it to the pending list (unless 4096 are pending already).
17. **[DetectiveMode.cpp:164-189](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L164-L189)** builds a map from `t_outconnect*` to the `Connection` components showing it, over every open editor and canvas. It's rebuilt at most once per tick. It also drops the history of connections that no longer exist.
18. **[DetectiveMode.cpp:215-248](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L215-L248)** takes the next pending message whose connection is showing right now (not in a background tab). It makes one `Bubble` for each place that connection shows, and starts at progress 0.
19. **[DetectiveMode.cpp:250-261](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L250-L261)** ends the message being shown: it repaints the canvas where the bubble was, and clears `current`.
20. **[DetectiveMode.cpp:124-140](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L124-L140)** has pause, step and history. Step pauses, but lets the message being shown finish its connection; the next one then waits at the start of its connection.
21. **[DetectiveMode.cpp:142-162](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L142-L162)** turns a message into text: its selector, then its atoms, cut off with "…" when it's longer than the limit.

#### D. Drawing it

22. **[DetectiveMode.cpp:299-345](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L299-L345)** works out where things go. The dot sits at `progress` times the path's length along the connection's path. The text is wrapped so the bubble fits in the view, and the bubble goes next to the dot. Then it repaints the old area and the new one.
23. **[DetectiveMode.cpp:20-60](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L20-L60)** are the sizes (padding, font, widest bubble) and two helpers. One wraps the text between words, or inside a word that's too long. The other places the bubble: right of the dot unless only the left has room, and above it unless only below has room.
24. **[DetectiveMode.cpp:347-377](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/DetectiveMode.cpp#L347-L377)** is `render`, for one canvas: a rounded box, the text inside it, and the dot as three circles (a glow, white, then the data connection colour).
25. **[Canvas.cpp:723-726](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Canvas.cpp#L723-L726)** calls `render` from `Canvas::performRender`, after every object and connection is drawn, so the message is on top. It runs on the message thread, like all other canvas drawing.
26. **[Connection.h:111-112](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Connection.h#L111-L112)** moves `getPathWidth()` from private to public. Nothing else about it changes. Detective Mode needs the connection's width to size the dot.

#### E. The controls in the status bar

27. **[Statusbar.cpp:208-227](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Statusbar.cpp#L208-L227)** is the "Detective" toggle: a text button that's lit while it's on, like the edit mode button.
28. **[Statusbar.cpp:490-502](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Statusbar.cpp#L490-L502)** creates the toggle and the speed slider. The toggle refers to the `detective_mode` setting and the slider to `detective_speed`: from 0.1 to 5, and a double-click puts it back to 1. The slider works whether Detective Mode is on or off, so the speed can be set before it's on.
29. **[Statusbar.cpp:581-592](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Statusbar.cpp#L581-L592)** gives them their place in the bar. The mode button takes its 84 px on the right first (line 581, moved up from below). The toggle and the slider then only take room that's left over, 72 px and 90 px, and each is hidden when there isn't room for it. So in a narrow status bar the slider hides first, then the toggle, and the Edit/Run button keeps its place.

### Commit 3: the sidebar panel

30. **[ConnectionDebugPanel.h:14-63](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/ConnectionDebugPanel.h#L14-L63)** declares the panel: a Pause button, a Step button, a speed slider and the list of messages, with its `ListBoxModel`.
31. **[ConnectionDebugPanel.cpp:17-55](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/ConnectionDebugPanel.cpp#L17-L55)** is the constructor. The slider refers to the same `detective_speed` setting as the one in the status bar, so they move together.
32. **[ConnectionDebugPanel.cpp:57-80](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/ConnectionDebugPanel.cpp#L57-L80)** sets which connection the panel shows, and refreshes the panel 10 times a second, only while it's visible.
33. **[ConnectionDebugPanel.cpp:82-119](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/ConnectionDebugPanel.cpp#L82-L119)** is the refresh. The buttons are only enabled while Detective Mode is on, and the Pause button reads "Play" while paused. An empty list says why: "Detective Mode is off", "Select a connection" or "No messages yet". The list is rebuilt, newest first, only when something changed.
34. **[ConnectionDebugPanel.cpp:121-158](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/ConnectionDebugPanel.cpp#L121-L158)** paints the panel: its background, the placeholder text, the layout, and one row of the list.
35. **[Connection.cpp:487-489](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Connection.cpp#L487-L489)** tells the panel when a connection is selected, so it shows that connection's messages.
36. **[PluginEditor.cpp:268](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginEditor.cpp#L268)** creates the panel, **[PluginEditor.cpp:276](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginEditor.cpp#L276)** hands it to both sidebars, and **[PluginEditor.cpp:300](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginEditor.cpp#L300)** loads which sidebar its tab is in, the right one by default.
37. `Sidebar.cpp` learns about the new panel in each place that lists the panels, following the reference panel's pattern:
    - **[Sidebar.cpp:45-46](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.cpp#L45-L46)** and **[Sidebar.cpp:68-69](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.cpp#L68-L69)**: its name in the settings file, "debug", both ways.
    - **[Sidebar.cpp:165-167](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.cpp#L165-L167)**: its tab's icon.
    - **[Sidebar.cpp:711-714](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.cpp#L711-L714)**: showing it when its tab is clicked.
    - **[Sidebar.cpp:1021-1022](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.cpp#L1021-L1022)**: which component belongs to its tab.
    - **[Sidebar.cpp:560-561](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.cpp#L560-L561)**: selecting something normally shows the Inspector over the current panel. Not over this one, or selecting a connection would hide the list you selected it for.
38. **[Sidebar.h:116](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.h#L116)** adds `DebugPanel` to the panel enum, **[Sidebar.h:236](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.h#L236)** gives it the display name "Detective Mode", and **[SettingsFile.h:205](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Utility/SettingsFile.h#L205)** sets its default side, right.

### The one-line hookups

- **`#include` lines:** [Canvas.cpp:15](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Canvas.cpp#L15), [Connection.cpp:27](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Connection.cpp#L27), [OverlayDisplaySettings.h:10](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Dialogs/OverlayDisplaySettings.h#L10), [PluginEditor.cpp:49](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginEditor.cpp#L49), [PluginMode.h:11](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginMode.h#L11), [PluginProcessor.cpp:29](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginProcessor.cpp#L29), [Sidebar.cpp:28](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.cpp#L28).
- **Forward declarations:** [PluginEditor.h:46](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginEditor.h#L46), [PluginProcessor.h:39](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginProcessor.h#L39), [Sidebar.h:21](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.h#L21), [Statusbar.h:20](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Statusbar.h#L20).
- **Members:** [PluginProcessor.h:213](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginProcessor.h#L213), [PluginEditor.h:168](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/PluginEditor.h#L168), [Statusbar.h:58-59](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Statusbar.h#L58-L59), [Sidebar.h:222](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.h#L222).
- **The sidebar's constructor takes the panel:** [Sidebar.h:131](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.h#L131), [Sidebar.cpp:96](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.cpp#L96), [Sidebar.cpp:107](https://github.com/topemalheiro/plugdata/blob/66bedae15bbec82c71be3a77beec7bc40c865d5d/Source/Sidebar/Sidebar.cpp#L107).

Blocks 4, 16 and 15 are the feature: Pd records each message (4), plugdata collects them (16) and plays them one at a time (15). Blocks 22 to 25 draw it. Everything else is controls and hookups.

## Threading (blocks 4, 5, 9 and 16)

Pd sends its messages on whichever thread is running Pd, usually the audio thread, with Pd's lock held. The bubbles are drawn on the message thread. The queue is how messages get from one to the other. Pd's side only tries to put a message in:

```cpp
        // Built in place, and only if there's room
        self->queue.try_emplace(static_cast<t_outconnect*>(target), selector, ++self->sequenceCounter, argc, argv);
```

`try_emplace` never waits and never allocates. If the queue is full, that message is lost: losing a bubble is better than an audio dropout. The queue is single-producer, single-consumer. The producer side is serialised by Pd's lock, and only the message thread drains it, 60 times a second (block 16).

Detective Mode takes Pd's lock only for the moment it registers or unregisters the collector (block 9):

```cpp
        pd->lockAudioThread();
        traceCollector = std::make_unique<pd::ConnectionTraceCollector>(gensym("_activity"));
        register_trace_trigger(instance, traceCollector.get(), &pd::ConnectionTraceCollector::enqueue);
        pd->unlockAudioThread();
```

Pd only calls the trace callback with its lock held. So it can never be in the middle of calling a collector that's being created or deleted. Everything else (the pending list, the history, the bubbles, `render`) runs on the message thread only.

How it was tested, and what wasn't, is in the PR description.
