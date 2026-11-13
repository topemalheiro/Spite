
## The two PRs on this desktop

**Why there are two:** plugdata's Pd lives in a separate repo (plugdata-team/plugdata-pd). Detective Mode needs a small change inside Pd, so that change is its own PR, and the plugdata PR points at it.

### PR 1, in plugdata-pd: let plugdata see every cable message, in order (18 lines)

**Branch:** `pr/detective-mode-hook`, commit `96fe52e4`, on upstream `master`.

When an object sends something out of an outlet, Pd calls `plugdata_forward_message()` once for each cable ([m_obj.c:592-646](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode-pd/src/m_obj.c#L592-L646)). Today only plugdata's message dispatcher listens to that call. The dispatcher keeps only the latest message of each kind per cable ([MessageListener.h:178](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Pd/MessageListener.h#L178)), which is fine for a tooltip but not for a debugger. This PR adds a second listener slot for Detective Mode. Nothing changes for Pd while no listener is plugged in: it costs one check per message.

* [s_inter.h:7](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode-pd/src/s_inter.h#L7): the listener's function type, the same shape as the existing one.
* [s_inter.h:10](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode-pd/src/s_inter.h#L10): `register_trace_trigger()`, to plug a listener in, or unplug it with null.
* [s_inter.c:180-181](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode-pd/src/s_inter.c#L180-L181): the two fields where Pd keeps the listener: the function, and the object to call it with.
* [s_inter.c:204-213](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode-pd/src/s_inter.c#L204-L213): `register_trace_trigger()` itself, which stores them.
* [s_inter.c:2521-2523](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode-pd/src/s_inter.c#L2521-L2523): **the hook.** After the existing dispatcher call, the listener gets the same message, if one is plugged in and debugging is on.

### PR 2, in plugdata: Detective Mode itself (3 commits, 20 files, +898/−9)

**Branch:** `pr/detective-mode`, on upstream `develop`'s newest commit.

Detective Mode shows the order in which Pd runs messages, which a patch never shows you. Each message becomes a dot with a bubble ("bang", "float 3") that travels along its cable, one at a time, in Pd's order. You can set the speed (0.1x to 5x), pause, and step one message at a time. A sidebar panel lists the selected cable's last messages ("#12 float 3"). It's off by default.

**Getting messages out of Pd (commit 2)**

* [ConnectionTraceCollector.h:14-34](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Pd/ConnectionTraceCollector.h#L14-L34): one recorded message: which cable, the message, up to 32 values, and its number in the order.
* [ConnectionTraceCollector.h:36-78](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Pd/ConnectionTraceCollector.h#L36-L78): the function PR 1's hook calls.
  * It runs on Pd's thread (often the audio thread), so it only copies the message into a lock-free queue and returns.
  * If the queue is full, it drops the message rather than make Pd wait.

**The player (commit 2)**

* [DetectiveMode.h:23-59](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/DetectiveMode.h#L23-L59): what the rest of plugdata can ask of it (on/off, pause, step, speed, a cable's history), and which thread does what.
* [DetectiveMode.h:61-118](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/DetectiveMode.h#L61-L118): what it keeps: the waiting messages, the one playing, each cable's last 64 messages, and the constants (400 ms per cable at 1x).
* [DetectiveMode.cpp:62-122](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L62-L122): turning it on plugs the collector into the hook and starts a timer that ticks 60 times a second. Turning it off unplugs it and clears everything. Both happen with Pd's lock held.
* [DetectiveMode.cpp:124-162](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L124-L162): pause, step, looking up a cable's history, and turning a message into text.
* [DetectiveMode.cpp:164-213](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L164-L213): matching Pd's cables to the cables drawn in each window, and taking new messages out of the queue.
* [DetectiveMode.cpp:215-297](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L215-L297): playback.
  * It starts the next message, skipping cables you can't see.
  * It moves the bubble each tick and stops it at the inlet.
  * It handles pause and step.
* [DetectiveMode.cpp:20-60](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L20-L60) and [299-377](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/DetectiveMode.cpp#L299-L377): the bubble itself: wrapping its text, placing it next to the dot, repainting only where it moved, and drawing it.

**Plugging it into plugdata (commit 2)**

* [Canvas.cpp:723-726](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Canvas.cpp#L723-L726): each canvas draws the bubbles after its objects and cables, so they sit on top.
* [PluginProcessor.cpp:194-195](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/PluginProcessor.cpp#L194-L195) and [207-209](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/PluginProcessor.cpp#L207-L209): one Detective Mode per plugdata instance, created once Pd is set up and deleted before the patches.
* [SettingsFile.h:152-153](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Utility/SettingsFile.h#L152-L153): its two settings: on/off (off by default) and speed.
* [OverlayDisplaySettings.h:162](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Dialogs/OverlayDisplaySettings.h#L162) and [PluginMode.h:156](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/PluginMode.h#L156): turning off "Debug", or leaving plugin mode, no longer stops Pd sending messages while Detective Mode needs them.
* [Statusbar.cpp:208-227](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Statusbar.cpp#L208-L227), [490-507](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Statusbar.cpp#L490-L507) and [586-589](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Statusbar.cpp#L586-L589): the "Detective" button, and the speed slider that shows only while it's on.
* [Connection.h:111-112](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Connection.h#L111-L112): makes a cable's thickness readable, so the dot can match it.

**The sidebar panel (commit 3)**

* [ConnectionDebugPanel.h:14-63](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Sidebar/ConnectionDebugPanel.h#L14-L63) and [ConnectionDebugPanel.cpp:17-158](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Sidebar/ConnectionDebugPanel.cpp#L17-L158): Pause/Play, Step, Speed (the same setting as the status bar), and the selected cable's messages, newest first.
* [Connection.cpp:487-489](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Connection.cpp#L487-L489): selecting a cable tells the panel which one to list.
* [Sidebar.cpp:711-714](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L711-L714): showing the panel.
* [Sidebar.cpp:560-561](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L560-L561): selecting something doesn't pop the inspector over the panel.
* [PluginEditor.cpp:268-300](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/PluginEditor.cpp#L268-L300): creating the panel and putting it in the right sidebar by default.

**One-line hookups** (includes, members, ids, names, the icon), if you want to read every line:

* [Canvas.cpp:15](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Canvas.cpp#L15);
* [PluginProcessor.cpp:29](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/PluginProcessor.cpp#L29);
* [PluginProcessor.h:39, 213](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/PluginProcessor.h#L39);
* [Statusbar.h:20, 58-59](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Statusbar.h#L20);
* [SettingsFile.h:205](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Utility/SettingsFile.h#L205);
* [OverlayDisplaySettings.h:10](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Dialogs/OverlayDisplaySettings.h#L10);
* [PluginMode.h:11](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/PluginMode.h#L11);
* [Connection.cpp:27](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Connection.cpp#L27);
* [Sidebar.h:21, 116, 131, 222, 236](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Sidebar/Sidebar.h#L21);
* [Sidebar.cpp:28, 45-46, 68-69, 96, 107, 165-167, 1017-1018](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/Sidebar/Sidebar.cpp#L28);
* [PluginEditor.h:46, 168](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/PluginEditor.h#L46);
* [PluginEditor.cpp:49](vscode-webview://0nc0lvf5fjuc7vgpv355pn6jc4qijg13dn09h3670igjhhhb1o8h/home/tope/Projects/plugdata-prs/detective-mode/Source/PluginEditor.cpp#L49).
