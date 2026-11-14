# hiddenWindow101: don't draw while the window can't be seen ([plugdata#2431](https://github.com/plugdata-team/plugdata/pull/2431))

**The links expect** `~/Projects/plugdata-prs/host-fixes` to be checked out on `review/host-fixes` (it is). That branch holds all three of my plugdata PRs together. The four files this PR changes are identical there to the PR's own branch `pr/host-fixes-hidden`, so checking that branch out instead gives the same lines.

## What this PR is, in plain words

**What you run into without it.** A host can hide a plugin's window instead of closing it. Ardour, and so Spite, does that with VST3 plugins: closing the window only hides it ([vst3_x11_plugin_ui.cc:162-165](../../Spite/gtk2_ardour/vst3_x11_plugin_ui.cc#L162-L165)). An LV2 window like plugdata's is really closed there, and its UI freed ([lv2_plugin_ui.cc:745-761](../../Spite/gtk2_ardour/lv2_plugin_ui.cc#L745-L761)). While its window is hidden, plugdata isn't told. It keeps drawing every change (number boxes, meters, anything that moves) into a window nobody can see, which costs CPU and GPU for nothing. My test didn't use a real DAW: a small test host hid its window for 4 seconds while a patch updated a number box 30 times a second. plugdata kept using 434-831 ms of CPU per second while hidden; with the fix, 27-80. A minimised plugdata standalone window on Linux should be the same case, but that wasn't tested.

**Why plugdata isn't told.** On Linux with X11, windows sit inside each other: the host's plugin window contains plugdata's window. When the host hides its window, everything inside disappears with it, but X only tells the window that was hidden, not the windows inside it. You can still ask X "can this window be seen?". X calls that "viewable": the window and every window it sits in are shown.

**What the PR changes.**
- Before drawing, plugdata asks X whether its window is viewable. Asking costs a round trip to the X server, so while the answer is yes it asks at most every 200 ms.
- While the window is hidden, it skips drawing and checks again every 50 ms on its own.
- When the window is back, it redraws everything.
- On Wayland nothing changes, because the helper always answers "visible" there.

**What it needs first.** Nothing. Linux and BSD only.

How plugdata draws, as background: a change marks a rectangle as "damaged" (`invalidArea`). `recordFrame`, on the GUI thread, draws the damaged part, and another thread hands that to the graphics card.

## The code, in reading order

1. **[OSUtils.h:43](../../plugdata-prs/host-fixes/Source/Utility/OSUtils.h#L43)** announces the new helper in the Linux part of `OSUtils`, plugdata's box of operating-system functions. Its code is in blocks 2 and 3.
   Python: `def is_linux_window_viewable(peer) -> bool: ...` in a stub file.
2. **[OSUtils.cpp:252-261](../../plugdata-prs/host-fixes/Source/Utility/OSUtils.cpp#L252-L261)**: the cases where it can't ask, so it answers "visible" and nothing changes, as the comment on line 254 says. That's no window, or a Wayland window (lines 255-256), and no connection to X or no window id (lines 258-261). `peer` is JUCE's handle on the window; `display` is the connection to the X server.
   Python: `if peer is None or is_wayland(peer): return True`, then `if not display or not window: return True`.
3. **[OSUtils.cpp:263-269](../../plugdata-prs/host-fixes/Source/Utility/OSUtils.cpp#L263-L269)** asks X. It takes the lock on the shared X connection (line 264) and asks for the window's attributes (line 266). If that fails it answers "visible". Otherwise the answer is `map_state == IsViewable`, X's "this window and all its parents are shown".
   Python: `with x_lock: attrs = x11.get_window_attributes(display, window)`, then `return attrs.map_state == VIEWABLE if attrs else True`.
4. **[NVGSurface.h:213-216](../../plugdata-prs/host-fixes/Source/NVGSurface.h#L213-L216)** announces two new private methods of `NVGSurface`, the part of plugdata that draws the editor. They exist on Linux and BSD only (see "Code that only exists on some systems").
   Python: two `def` lines with `...` bodies in a class.
5. **[NVGSurface.h:247-250](../../plugdata-prs/host-fixes/Source/NVGSurface.h#L247-L250)** adds two fields: the last answer (starting as "visible") and when X was last asked, in milliseconds.
   Python: `self.window_viewable = True` and `self.last_viewable_check = 0` in `__init__`.
6. **[NVGSurface.cpp:529-549](../../plugdata-prs/host-fixes/Source/NVGSurface.cpp#L529-L549)**: `isWindowViewable`, the cached question.

   * Lines 536-538: if the window was visible and X was asked less than 200 ms ago, answer "visible" without asking.
   * Lines 539-542: otherwise note the time and ask X (block 3).
   * Lines 543-544: if the window just came back, mark the whole editor as damaged (`invalidateAll()`), so the next frame redraws everything. A hidden window loses its picture.
   * Lines 545-546: if it just disappeared, start checking every 50 ms (block 7).

   Python: `if self.window_viewable and now - self.last_check < 0.2: return True`, then ask, and on a change call `self.invalidate_all()` or `self.poll_until_viewable()`.
7. **[NVGSurface.cpp:551-557](../../plugdata-prs/host-fixes/Source/NVGSurface.cpp#L551-L557)**: `pollUntilViewable`. In 50 ms, ask again; if the window is still hidden, schedule the same again. It's a chain of one-shot timers that ends once the window is back, and that last check also triggers the full redraw. It's needed because a still patch asks for no drawing at all, so nothing else would ever check (see "A function to run later").
   Python: `ref = weakref.ref(self)`, then `loop.call_later(0.05, lambda: (s := ref()) and not s.is_window_viewable() and s.poll_until_viewable())`.
8. **[NVGSurface.cpp:690-694](../../plugdata-prs/host-fixes/Source/NVGSurface.cpp#L690-L694)**: at the top of `recordFrame`, if the window can't be seen, return without drawing. The damage stays in `invalidArea`, so nothing is lost: it's drawn once the window is back.
   Python: `if not self.is_window_viewable(): return`.

## Code that only exists on some systems: `#if` (blocks 4, 5, 6, 7, 8)

```cpp
#if JUCE_LINUX || JUCE_BSD
    bool isWindowViewable();
    void pollUntilViewable();
#endif
```

`#if` isn't a normal `if`. It runs before compiling, while the code is still text: on macOS or Windows these lines are removed and never compiled at all. `JUCE_LINUX` is a name JUCE sets to 1 when building on Linux.

Python comparison: `if sys.platform == "linux":` around a `def`, except that it's decided once, when building, and the other systems' builds don't contain the code.

## A class written in two files (blocks 4 to 8)

C++ classes are usually announced in a `.h` file and written in a `.cpp` file. `NVGSurface.h` lists the methods and fields inside `class NVGSurface { ... };` (blocks 4 and 5). `NVGSurface.cpp` writes each method's code, with the class's name in front (block 6):

```cpp
bool NVGSurface::isWindowViewable()
{
    auto const now = Time::getMillisecondCounter();
    if (windowViewable && now - lastViewableCheck < 200)
        return true;
```

`NVGSurface::` means "this belongs to class NVGSurface". Inside a method, fields like `windowViewable` are used without `self.`: C++ adds the `this->` (its `self`) for you. `auto` lets the compiler work out the type, and `const` means the value can't change afterwards.

Python comparison: `def is_window_viewable(self):` written inside the class, using `self.window_viewable`.

## A lock that releases itself (block 3)

```cpp
    juce::XWindowSystemUtilities::ScopedXLock xLock;
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, window, &attributes))
        return true;
```

plugdata and JUCE share one connection to the X server, so only one thread may use it at a time. `ScopedXLock xLock;` creates an object that takes that lock as it's created and releases it when it goes away, at the function's closing `}` or at any `return`. C++ calls this pattern RAII.

Python comparison: `with x_lock:` around the rest of the function.

## Filling a struct through its address (block 3)

Many C functions don't return their result. They return "did it work?" and write the result into a struct you give them by address. `&attributes` is that address, and `XGetWindowAttributes` fills it in.

Python comparison: `attributes = get_window_attributes(display, window)`, with `None` if it failed.

## A function to run later, which may outlive its object (block 7)

```cpp
    Timer::callAfterDelay(50, [_this = SafePointer(this)] {
        if (_this && !_this->isWindowViewable())
            _this->pollUntilViewable();
    });
```

`[ ... ] { ... }` is a lambda, a function written in place. The part between `[` and `]` is what it carries along: `_this`, a `SafePointer` to this editor's surface. The editor may be closed within those 50 ms. A `SafePointer` turns into null if its object is deleted, so `if (_this && ...)` checks first, and the lambda never touches a deleted object. `Timer::callAfterDelay` runs the lambda on the GUI thread after the delay.

Python comparison: `ref = weakref.ref(self)`, then `loop.call_later(0.05, check)`, where `check` starts with `s = ref(); if s is None: return`.

Block 8 is the actual fix: no drawing while the window is hidden. Blocks 6 and 7 decide when that is and redraw everything when the window is back; blocks 1 to 5 are plumbing.
