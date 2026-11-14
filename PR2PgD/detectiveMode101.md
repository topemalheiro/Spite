# detectiveMode101: Detective Mode (plugdata PR)

**The links expect** `~/Projects/plugdata-prs/detective-mode` to be checked out on `pr/detective-mode` (it is). This is PR 13 in [order.md](order.md).

## What this PR is, in plain words

**What you run into without it.** When a patch does the wrong thing, you can't see what happened in which order. plugdata can already show a cable's latest message when you hover over it, and flash objects when they get a message. But Pd runs a whole chain of messages in one instant, in an order that depends on outlet positions and on how the patch is wired. That order is where most patching bugs come from, and today you work it out in your head or with `[print]` objects.

**What the PR changes.** The status bar gets a "Detective" button. While it's on, every message Pd sends through a cable shows up as a dot travelling along that cable, with the message's text in a box next to it. They play one at a time, in the exact order Pd sent them. A slider next to the button sets the speed. A new sidebar panel can pause, step through one message at a time, and list the last 64 messages of the cable you select.

**How it works, in three steps.**

1. Pd writes every cable message into a queue, at the moment it sends it. This uses the hook from PR 1 ([pdHook101.md](pdHook101.md)).
2. 60 times a second, plugdata empties that queue into a waiting list, and plays the waiting list one message at a time.
3. The canvas draws the dot and the box. The status bar and the sidebar panel are the controls.

**How big it is.** 3 commits, 20 files, 902 lines added and 10 removed. Five new files hold 797 of those lines. The other 105 are small additions to 15 existing files. Nothing is specific to one operating system.

**What it needs first.** PR 1, the trace hook in plugdata-pd. Commit 1 of this PR makes plugdata use a Pd that has it.

## The code, in reading order

### Commit 1: use the Pd that has the hook

1. **[Libraries/pure-data](../../plugdata-prs/detective-mode/Libraries/pure-data)** isn't lines of code. plugdata records exactly which commit of Pd it builds with. This commit moves that from 267a42a8 (upstream's) to caeb1359 (PR 1's commit).
   Python: changing a pinned version in `requirements.txt`, like `pure-data==caeb1359`.

### Commit 2: Detective Mode

#### A. Recording the messages inside Pd

All of this is one new file, `ConnectionTraceCollector.h`.

2. **[ConnectionTraceCollector.h:14-34](../../plugdata-prs/detective-mode/Source/Pd/ConnectionTraceCollector.h#L14-L34)** is one recorded message: which cable, the message's name, its number in the order, and a copy of up to 32 of its values. It copies the values because Pd reuses that memory as soon as the call returns. `truncated` says there were more than 32.
   Python: a `@dataclass` with `connection`, `selector`, `sequence_number`, `atoms = list(argv[:32])` and `truncated = len(argv) > 32`.
3. **[ConnectionTraceCollector.h:36-45](../../plugdata-prs/detective-mode/Source/Pd/ConnectionTraceCollector.h#L36-L45)** starts the collector, the object that holds the queue. It remembers the name `_activity`, which Pd uses for "this object just got a message" notices. Those travel the same road as cable messages, so block 4 needs the name to skip them.
   Python: `class ConnectionTraceCollector:` with `def __init__(self, activity_symbol): self.activity_symbol = activity_symbol`.
4. **[ConnectionTraceCollector.h:47-56](../../plugdata-prs/detective-mode/Source/Pd/ConnectionTraceCollector.h#L47-L56)** is the function Pd calls for every cable message. It's what gets plugged into the hook (block 5 of pdHook101).
   - It ignores anything that isn't a cable message (`type != 1`) and the `_activity` notices.
   - It numbers the message and puts it in the queue. If the queue is full, the message is dropped: Pd must never wait (see "Two threads and a queue").

   Python: `def enqueue(collector, kind, target, selector, argv):` then `if kind != 1 or selector == collector.activity_symbol: return`, then `collector.counter += 1` and `collector.queue.put_nowait(Event(...))`, ignoring `queue.Full`.
5. **[ConnectionTraceCollector.h:58-70](../../plugdata-prs/detective-mode/Source/Pd/ConnectionTraceCollector.h#L58-L70)** is plugdata's side of the queue. It hands the recorded messages to a function you give it, oldest first, at most 4096 per call (see "A template").
   Python: `def dequeue(self, callback):` with `for _ in range(4096): try: callback(self.queue.get_nowait())` and `except queue.Empty: break`.
6. **[ConnectionTraceCollector.h:72-77](../../plugdata-prs/detective-mode/Source/Pd/ConnectionTraceCollector.h#L72-L77)** is the collector's data: the queue with room for 4096 messages, the `_activity` name, and the counter that numbers the messages.
   Python: `self.queue = queue.Queue(maxsize=4096)` and `self.counter = 0`.

#### B. Turning it on and off

7. **[DetectiveMode.h:23-59](../../plugdata-prs/detective-mode/Source/DetectiveMode.h#L23-L59)** is what the rest of plugdata can use of Detective Mode: is it on, pause, step, draw, a cable's history, and `Message` (what one shown message holds). It only announces these; the code is in `DetectiveMode.cpp` (see "The class line").
   Python: the `class DetectiveMode(SettingsFileListener, Timer):` line and its public methods' `def` lines.
8. **[DetectiveMode.cpp:62-76](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L62-L76)** runs when Detective Mode is created and destroyed. Created: it ties its speed to the `detective_speed` setting, and turns on if the `detective_mode` setting says so. Destroyed: it unplugs itself from Pd.
   Python: `__init__` and `__del__`.
9. **[DetectiveMode.cpp:90-122](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L90-L122)** turns it on or off.
   - **On:** it locks Pd, creates the collector, plugs it into the hook with `register_trace_trigger`, and unlocks. Then it starts a timer that fires 60 times a second (block 15).
   - **Off:** it stops the timer, locks Pd, unplugs, unlocks, deletes the collector and forgets every message.
   - Either way, it then updates Pd's debug switch (block 10).

   Python: `def set_enabled(self, on):` with `with pd.lock: self.collector = Collector(...); register_trace_trigger(instance, self.collector, Collector.enqueue)`.
10. **[DetectiveMode.cpp:78-88](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L78-L88)** has two small functions. The first sets Pd's "report cable messages" switch: on if the "Debug connections" option or Detective Mode is on. The second runs whenever a setting changes, and turns Detective Mode on or off when that setting is `detective_mode`.
    Python: `set_debugging(settings["debug_connections"] or settings["detective_mode"])`, and `def settings_changed(self, name, value): if name == "detective_mode": self.set_enabled(bool(value))`.
11. **[OverlayDisplaySettings.h:162](../../plugdata-prs/detective-mode/Source/Dialogs/OverlayDisplaySettings.h#L162)** and **[PluginMode.h:156](../../plugdata-prs/detective-mode/Source/PluginMode.h#L156)** are the two places that used to set Pd's debug switch from the "Debug connections" option alone. They now call block 10's function, so switching that option off, or leaving plugin mode, doesn't cut off Detective Mode's messages. **[Connection.cpp:578-579](../../plugdata-prs/detective-mode/Source/Connection.cpp#L578-L579)** is the other side of that. The little message box you get when you hover over a cable used to show whenever Pd's debug switch was on. Detective Mode now turns that switch on too, so the hover box also checks the "Debug connections" option itself. Turning Detective Mode on doesn't bring the hover boxes back when you've turned that option off.
    Python: replacing `set_debugging(settings["debug_connections"])` with `DetectiveMode.update_pd_debugging()`, and for the hover box `if debugging_enabled() and settings["debug_connections"]:`.
12. **[PluginProcessor.cpp:194-195](../../plugdata-prs/detective-mode/Source/PluginProcessor.cpp#L194-L195)** creates the one Detective Mode each plugdata has, and **[PluginProcessor.cpp:207-209](../../plugdata-prs/detective-mode/Source/PluginProcessor.cpp#L207-L209)** deletes it first when plugdata closes, so Pd stops recording before the patches are deleted (see "`unique_ptr`").
    Python: `self.detective_mode = DetectiveMode(self)` in `__init__`, and `self.detective_mode = None` at the top of the cleanup.
13. **[SettingsFile.h:152-153](../../plugdata-prs/detective-mode/Source/Utility/SettingsFile.h#L152-L153)** adds the two settings with their starting values: off, and speed 1.
    Python: two more entries in a defaults dict, `"detective_mode": False, "detective_speed": 1.0`.

#### C. Playing the messages

14. **[DetectiveMode.h:61-118](../../plugdata-prs/detective-mode/Source/DetectiveMode.h#L61-L118)** is Detective Mode's private data.
    - `Bubble`: what's drawn for the message in one canvas (its cable, its text layout, where the box and dot are).
    - `pending`: the waiting list. `history`: the last messages per cable. `current`: the message being shown. `progress`: how far along the cable it is, from 0 to 1.
    - `paused`, `stepping`, `speed`.
    - The limits: 400 ms per cable at speed 1, 4096 waiting messages, 64 messages of history per cable, 300 characters of text in a box.

    Python: the `self.` variables set in `__init__`, and class constants like `MESSAGE_DURATION_MS = 400`.
15. **[DetectiveMode.cpp:263-297](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L263-L297)** is the timer, 60 times a second. This is where the playing happens.
    - It collects the new messages (block 16).
    - If a message is showing, it moves it forward: the time since the last tick, times the speed, divided by 400 ms. Paused means it doesn't move. A long freeze counts as 50 ms at most, so the message carries on from where it was.
    - It ends the message when it reaches the end of the cable, when the cable was deleted, or when the cable can't be seen while playing.
    - If nothing is showing, it starts the next one (block 18). Then it updates the drawing (block 22).

    Python: `def timer_callback(self):` with `self.collect_messages()`, `self.progress += elapsed_ms * speed / 400`, `if self.progress >= 1: self.stop_message()`, `if self.current is None: self.start_next_message()`.
16. **[DetectiveMode.cpp:191-213](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L191-L213)** empties the collector's queue (see "Lambdas"). For each recorded message: skip it if its patch isn't open, turn it into a `Message`, add it to its cable's history (dropping the oldest above 64), and add it to the waiting list (unless 4096 are waiting already).
    Python: `self.collector.dequeue(handle)` where `handle(event)` does `self.history[event.connection].append(message)` and `self.pending.append((event.connection, message))`.
17. **[DetectiveMode.cpp:164-189](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L164-L189)** builds a lookup from Pd's cable to the cable drawn on screen, over every open window and tab. It's rebuilt at most once per tick. It also drops the history of cables that no longer exist.
    Python: `open_connections = {c.pointer: (canvas, c) for editor in editors for canvas in editor.canvases for c in canvas.connections}`.
18. **[DetectiveMode.cpp:215-248](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L215-L248)** takes the next waiting message whose cable can be seen right now (not in a background tab). It makes one `Bubble` for each place that cable shows, and starts at progress 0.
    Python: `while self.pending: connection, message = self.pending.popleft()`, then `if visible: self.current = message; self.progress = 0; return True`.
19. **[DetectiveMode.cpp:250-261](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L250-L261)** ends the message being shown: it asks the canvas to redraw where the box was, and clears `current`.
    Python: `canvas.repaint(bubble.area)`, then `self.current = None`.
20. **[DetectiveMode.cpp:124-140](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L124-L140)** has pause, step and history. Step pauses, but lets the message being shown finish its cable; the next one then waits at the start of its cable.
    Python: `def step(self): self.paused = True; self.stepping = self.current is not None`.
21. **[DetectiveMode.cpp:142-162](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L142-L162)** turns a message into text: its name, then its values, cut off with "…" when it's longer than the limit.
    Python: `text = " ".join([name, *map(str, atoms)])`, then `text[:max_length] + " …"` when it's too long.

#### D. Drawing it

22. **[DetectiveMode.cpp:299-345](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L299-L345)** works out where things go. The dot sits at `progress` times the cable's length along the cable's curve. The text is wrapped so the box fits in the view, and the box goes next to the dot. Then it asks the canvas to redraw the old spot and the new one.
    Python: `dot = path.point_at(progress * path.length)`, `box = place_bubble(view, dot, size)`, `canvas.repaint(old_area | new_area)`.
23. **[DetectiveMode.cpp:20-60](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L20-L60)** are the sizes (padding, font, widest box) and two helpers. One wraps the text between words, or inside a word that's too long. The other places the box: right of the dot unless only the left has room, and above it unless only below has room.
    Python: module constants like `BUBBLE_MAX_WIDTH = 560`, and two module-level functions.
24. **[DetectiveMode.cpp:347-377](../../plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L347-L377)** draws it for one canvas: a rounded box, the text inside it, and the dot as three circles (a glow, white, then the colour of message cables).
    Python: `def render(self, canvas, nvg, region):` with `draw_rounded_rect(box)`, `draw_text(layout)` and three `draw_circle(dot, radius, colour)` calls.
25. **[Canvas.cpp:723-726](../../plugdata-prs/detective-mode/Source/Canvas.cpp#L723-L726)** makes the canvas call block 24 after it has drawn every object and cable, so the message is on top.
    Python: `if self.pd.detective_mode: self.pd.detective_mode.render(self, nvg, region)` as the last step of the canvas's draw function.
26. **[Connection.h:111-112](../../plugdata-prs/detective-mode/Source/Connection.h#L111-L112)** moves `getPathWidth()` from the cable's private part to its public part. Nothing else about it changes. Detective Mode needs the cable's width to size the dot.
    Python: renaming `_get_path_width` to `get_path_width`.

#### E. The controls in the status bar

27. **[Statusbar.cpp:208-227](../../plugdata-prs/detective-mode/Source/Statusbar.cpp#L208-L227)** is the "Detective" button: a text button that stays lit while it's on, like the edit mode button.
    Python: `class DetectiveModeButton(TextButton):` with its own `paint`.
28. **[Statusbar.cpp:490-502](../../plugdata-prs/detective-mode/Source/Statusbar.cpp#L490-L502)** creates the button and the speed slider. The button is tied to the `detective_mode` setting and the slider to `detective_speed`: from 0.1 to 5, and a double-click puts it back to 1. The slider works whether Detective Mode is on or off, so the speed can be set before it's on (see "One setting, shared").
    Python: `self.button = DetectiveModeButton()`, `self.slider = Slider(min=0.1, max=5.0, step=0.1)`.
29. **[Statusbar.cpp:581-592](../../plugdata-prs/detective-mode/Source/Statusbar.cpp#L581-L592)** gives them their place in the bar.
    - Line 581 gives the Edit/Run button its 84 pixels on the right first. That line was already there, further down; it moved up so that button always gets its room.
    - Lines 583-587: the Detective button takes 72 pixels of what's left. If there aren't 72, it's hidden.
    - Lines 589-591: the slider takes 90 pixels of what's left after that. If there's no room for it, or the button is hidden, the slider is hidden too.
    - So in a narrow window the slider goes first, then the button, and the Edit/Run button stays. Widen the window and they come back.

    Python: `self.button.visible = bar.width >= 72`, then `if self.button.visible: self.button.bounds = bar.take_left(72)`, and the same for the slider with 90.

### Commit 3: the sidebar panel

30. **[ConnectionDebugPanel.h:14-63](../../plugdata-prs/detective-mode/Source/Sidebar/ConnectionDebugPanel.h#L14-L63)** announces the panel: a Pause button, a Step button, a speed slider and the list of messages. `HistoryModel` is a small helper class that tells the list how many rows it has and how to draw one.
    Python: `class ConnectionDebugPanel(Component, Timer):` and its `def` lines.
31. **[ConnectionDebugPanel.cpp:17-55](../../plugdata-prs/detective-mode/Source/Sidebar/ConnectionDebugPanel.cpp#L17-L55)** builds the panel. Each button gets the function to run on a click (see "Lambdas"). The slider is tied to the same `detective_speed` setting as the one in the status bar, so they move together.
    Python: `self.pause_button.on_click = lambda: detective_mode.set_paused(not detective_mode.is_paused())`.
32. **[ConnectionDebugPanel.cpp:57-80](../../plugdata-prs/detective-mode/Source/Sidebar/ConnectionDebugPanel.cpp#L57-L80)** sets which cable the panel shows, and refreshes the panel 10 times a second, only while it's visible.
    Python: `def set_connection(self, connection): self.connection = connection; self.update()`.
33. **[ConnectionDebugPanel.cpp:82-119](../../plugdata-prs/detective-mode/Source/Sidebar/ConnectionDebugPanel.cpp#L82-L119)** is the refresh. The buttons only work while Detective Mode is on, and the Pause button reads "Play" while paused. An empty list says why: "Detective Mode is off", "Select a connection" or "No messages yet". The list is rebuilt, newest first, only when something changed.
    Python: `self.items = [f"#{m.sequence_number}  {m}" for m in reversed(history)]`.
34. **[ConnectionDebugPanel.cpp:121-158](../../plugdata-prs/detective-mode/Source/Sidebar/ConnectionDebugPanel.cpp#L121-L158)** draws the panel: its background, the "why it's empty" text, where each control goes, and one row of the list.
    Python: `def paint(self, g):` and `def resized(self):`.
35. **[Connection.cpp:487-489](../../plugdata-prs/detective-mode/Source/Connection.cpp#L487-L489)** tells the panel when you select a cable, so it shows that cable's messages.
    Python: `if self.selected and editor.debug_panel: editor.debug_panel.set_connection(self)`.
36. **[PluginEditor.cpp:268](../../plugdata-prs/detective-mode/Source/PluginEditor.cpp#L268)** creates the panel, **[PluginEditor.cpp:276](../../plugdata-prs/detective-mode/Source/PluginEditor.cpp#L276)** hands it to both sidebars, and **[PluginEditor.cpp:300](../../plugdata-prs/detective-mode/Source/PluginEditor.cpp#L300)** puts its tab in the sidebar the settings say, the right one by default.
    Python: `self.debug_panel = ConnectionDebugPanel(self)`.
37. `Sidebar.cpp` learns about the new panel in each place that lists the panels:
    - **[Sidebar.cpp:45-46](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L45-L46)** and **[Sidebar.cpp:68-69](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L68-L69)**: its name in the settings file, "debug", both ways.
    - **[Sidebar.cpp:165-167](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L165-L167)**: its tab's icon.
    - **[Sidebar.cpp:711-714](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L711-L714)**: showing it when its tab is clicked.
    - **[Sidebar.cpp:1021-1022](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L1021-L1022)**: which component belongs to its tab.
    - **[Sidebar.cpp:560-561](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L560-L561)**: selecting something normally opens the Inspector over the current panel. Not over this one, or selecting a cable would hide the list you selected it for.

    Python: one more `case` in each `match panel:`, like `case Panel.DEBUG: return "debug"`.
38. **[Sidebar.h:116](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.h#L116)** adds the panel to the list of panel kinds, **[Sidebar.h:236](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.h#L236)** gives it the name "Detective Mode" on screen, and **[SettingsFile.h:205](../../plugdata-prs/detective-mode/Source/Utility/SettingsFile.h#L205)** sets its default side, right.
    Python: a new member in an `Enum`, a new string in a list, a new entry in the defaults dict.

### The one-line hookups

These only make names known to the files that use them.

- **`#include` lines**, each pulling in a header a file now needs (Python: `import`): [Canvas.cpp:15](../../plugdata-prs/detective-mode/Source/Canvas.cpp#L15), [Connection.cpp:27](../../plugdata-prs/detective-mode/Source/Connection.cpp#L27), [OverlayDisplaySettings.h:10](../../plugdata-prs/detective-mode/Source/Dialogs/OverlayDisplaySettings.h#L10), [PluginEditor.cpp:49](../../plugdata-prs/detective-mode/Source/PluginEditor.cpp#L49), [PluginMode.h:11](../../plugdata-prs/detective-mode/Source/PluginMode.h#L11), [PluginProcessor.cpp:29](../../plugdata-prs/detective-mode/Source/PluginProcessor.cpp#L29), [Sidebar.cpp:28](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L28).
- **Forward declarations**, saying "a class with this name exists" (see "Forward declarations"): [PluginEditor.h:46](../../plugdata-prs/detective-mode/Source/PluginEditor.h#L46), [PluginProcessor.h:39](../../plugdata-prs/detective-mode/Source/PluginProcessor.h#L39), [Sidebar.h:21](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.h#L21), [Statusbar.h:20](../../plugdata-prs/detective-mode/Source/Statusbar.h#L20).
- **Member variables**, the slots that hold the new objects (Python: `self.x = None` in `__init__`): [PluginProcessor.h:213](../../plugdata-prs/detective-mode/Source/PluginProcessor.h#L213), [PluginEditor.h:168](../../plugdata-prs/detective-mode/Source/PluginEditor.h#L168), [Statusbar.h:58-59](../../plugdata-prs/detective-mode/Source/Statusbar.h#L58-L59), [Sidebar.h:222](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.h#L222).
- **The sidebar's constructor takes the panel** (Python: one more parameter in `__init__`, stored on `self`): [Sidebar.h:131](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.h#L131), [Sidebar.cpp:96](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L96), [Sidebar.cpp:107](../../plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L107).

Blocks 4, 16 and 15 are the feature: Pd records each message (4), plugdata collects them (16) and plays them one at a time (15). Blocks 22 to 25 draw it. Everything else is controls and hookups.

## Two threads and a queue (blocks 4, 5, 9 and 16)

A thread is one line of work running inside the program. plugdata has two that matter here:

- **The thread that runs Pd.** Usually it's the audio thread, which has to deliver sound every few milliseconds. It must never wait for anything, or you hear a click.
- **The message thread.** It draws the window and handles clicks. It's allowed to be slow.

Pd sends its messages on the first, and the bubbles are drawn on the second. The queue is how messages get from one to the other. Pd's side only tries to put a message in:

```cpp
        // Built in place, and only if there's room
        self->queue.try_emplace(static_cast<t_outconnect*>(target), selector, ++self->sequenceCounter, argc, argv);
```

`try_emplace` never waits. If the queue is full, it does nothing and returns, and that message is lost. Losing a bubble is better than a click in the sound. The message thread empties the queue 60 times a second (block 16).

A lock is different: it makes one thread wait until the other is done. Detective Mode takes Pd's lock only for the moment it plugs the collector in or out (block 9):

```cpp
        pd->lockAudioThread();
        traceCollector = std::make_unique<pd::ConnectionTraceCollector>(gensym("_activity"));
        register_trace_trigger(instance, traceCollector.get(), &pd::ConnectionTraceCollector::enqueue);
        pd->unlockAudioThread();
```

While plugdata holds the lock, Pd isn't running. So Pd can never be in the middle of calling a collector that's being created or deleted.

Python: `queue.Queue(maxsize=4096)` with `put_nowait()` on one thread and `get_nowait()` on the other, and `with lock:` around the plugging in. Python's threads rarely run at the same instant; C++'s really do, which is why this needs care.

## `void*` and `static_cast`: getting the collector back from Pd (block 4)

Pd is written in C and knows nothing about plugdata's classes. So the hook stores the collector as a `void*`, which means "the address of something, I don't know what". When Pd calls `enqueue`, it hands that address back as the first argument, and `enqueue` says what it really is:

```cpp
    static void enqueue(void* collector, int const type, void* target, t_symbol* selector, int const argc, t_atom* argv)
    {
        auto* self = static_cast<ConnectionTraceCollector*>(collector);
```

`static` means the function has no hidden `self`: C can only call plain functions. `static_cast<ConnectionTraceCollector*>(collector)` means "treat this address as a collector". After that line, `self` is used the way Python uses `self`.

The same happens to `target`: Pd passes the cable as a `void*`, and the next lines cast it to `t_outconnect*`, Pd's type for a cable.

Python: a plain function that takes the object as its first argument, `def enqueue(collector, ...)`, registered as `register(collector, enqueue)`. Python doesn't need the cast, because every value knows its own type.

## `unique_ptr`: an object with exactly one owner (blocks 9, 12, 28 and 36)

C++ has no garbage collector. Something has to delete each object, exactly once. A `std::unique_ptr` is a variable that owns an object and deletes it when the variable goes away or is given something else.

```cpp
    std::unique_ptr<pd::ConnectionTraceCollector> traceCollector;
```

- `std::make_unique<X>(...)` creates an `X` and puts it in the variable.
- `traceCollector.get()` gives the plain address, for code that only uses the object and doesn't own it (the hook, in block 9).
- `traceCollector.reset()` deletes the object now and leaves the variable empty.
- `if (traceCollector)` asks "is there an object in it?". An empty one equals `nullptr`, C++'s `None`.

`isEnabled()` in block 7 is just that question: Detective Mode is on exactly while a collector exists.

Python: `self.collector = Collector()` and later `self.collector = None`, with the promise that nothing else keeps a reference.

## Lambdas: small functions written in place (blocks 16, 24 and 31)

```cpp
    traceCollector->dequeue([this](pd::ConnectionTraceEvent const& event) {
        // Messages in patches that aren't open can't be shown or selected
        if (!getOpenConnections().contains(event.connection))
            return;
```

Everything from `[this]` to the closing `}` is a function with no name, handed straight to `dequeue`. The square brackets list what the function may use from outside itself:

- `[this]`: the object it was written in, so it can reach `history` and `pending`.
- `[&]`: anything nearby, used directly.
- `[nvg, dot = bubble.dot]` (block 24): a copy of `nvg`, and a copy of `bubble.dot` under the name `dot`.

Python: a `lambda` or a nested `def`. Python lets it use outside variables without listing them.

## A template: a function that takes any callable (block 5)

```cpp
    template<typename Callback>
    void dequeue(Callback&& callback)
```

C++ wants to know the type of every argument. A lambda's type has no name you can write, so `dequeue` says "I work for any type; call it `Callback`". The compiler writes a version of `dequeue` for each type it's used with.

Python: `def dequeue(self, callback):`. Python never asks for the type, so it needs nothing like this.

## The class line (blocks 7 and 30)

```cpp
class DetectiveMode final : public SettingsFileListener
    , private Timer {
```

- `: public SettingsFileListener, private Timer` are the classes it builds on. From the first it gets `settingsChanged`, called when a setting changes. From the second it gets a timer. `private` means only Detective Mode itself can use the timer part.
- `final`: no class can build on this one.
- `override`, after a function: "this replaces a function of the class I build on". The compiler checks that one really exists.
- `const`, after a function: "calling this doesn't change the object".
- `public:` and `private:` split what the rest of plugdata may use from what only the class may use.

A class is written in two files, the same way as a function: the header (`.h`) announces what it has, and the `.cpp` holds the code. [pdHook101.md](pdHook101.md) explains that under "One function, written twice".

Python: `class DetectiveMode(SettingsFileListener, Timer):`. Python has no `private`; a leading underscore is the custom.

## Forward declarations (the one-line hookups)

```cpp
class DetectiveMode;
```

This tells the compiler "a class called DetectiveMode exists", and nothing more. It's enough for a header that only stores a pointer to one, like `PluginProcessor.h`. The alternative is to `#include "DetectiveMode.h"` there, and then every file that includes `PluginProcessor.h` would have to read all of Detective Mode's header too, which makes the build slower. The `.cpp` files that really use Detective Mode include the full header.

Python: importing a name only for type hints, under `if TYPE_CHECKING:`.

## `SafePointer`: a pointer that notices when its target is gone (blocks 14 and 30)

```cpp
        Component::SafePointer<Canvas> canvas;
        Component::SafePointer<Connection> connection;
```

A plain pointer is only an address. If the canvas is closed and deleted, the address still looks fine, and using it crashes. JUCE's `SafePointer` turns into `nullptr` by itself when its component is deleted. That's why the code can test it:

```cpp
        if (auto* cnv = bubble.canvas.getComponent())
            cnv->repaint(bubble.area);
```

A message can be on screen when you close its tab, so the bubbles need this.

Python: `weakref.ref(canvas)`, which returns `None` once the canvas is gone.

## One setting, shared (blocks 8, 10, 28 and 31)

```cpp
    detectiveModeButton->getToggleStateValue().referTo(SettingsFile::getInstance()->getPropertyAsValue("detective_mode"));
```

A JUCE `Value` is a box holding one value that several things can share. `referTo` makes the button's on/off state the same box as the `detective_mode` setting. So:

1. Clicking the button changes the setting, and the setting is saved.
2. The settings file tells everything that listens to it. Detective Mode listens (block 10), and turns itself on or off.

The button never calls Detective Mode directly. The speed works the same way: both sliders and Detective Mode's `speed` are the same `detective_speed` box, which is why moving one slider moves the other.

Python: an observable setting, where widgets and the feature all subscribe to changes of one key.

## Small C++ words in this PR

| C++ | What it means | Python |
| --- | --- | --- |
| `auto` | "work out the type yourself" | nothing needed |
| `const` | "this won't be changed" | nothing; a habit, not a rule |
| `constexpr` | a constant known when the program is built | a constant in capitals |
| `nullptr` | no object | `None` |
| `a->b` | `b` of the object that pointer `a` points at | `a.b` |
| `A::b` | the name `b` inside class or namespace `A` | `A.b` |
| `std::optional<Message>` | a `Message`, or nothing | `Message \| None` |
| `std::deque` | a list that's quick to add to and remove from at both ends | `collections.deque` |
| `UnorderedMap<K, V>` | a lookup from key to value | `dict` |
| `SmallArray<T, 2>` | a list with room for 2 items before it needs more memory | `list` |
| `std::move(x)` | hand over what `x` holds instead of copying it | nothing; Python never copies |
| `for (auto const& [cnv, connection] : pairs)` | loop over pairs, naming both halves | `for cnv, connection in pairs:` |
| `if (auto* cnv = f())` | get a value and test it in one go | `if (cnv := f()) is not None:` |
| `namespace { ... }` | names only this file can use | a leading underscore |
| `static`, on a function in a class | no hidden `self` | `@staticmethod` |
| `explicit`, on a constructor | no silent conversion into this class | nothing |
| `#pragma once` | read this header only once per file | imports already do that |
