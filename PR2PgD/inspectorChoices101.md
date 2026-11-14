# inspectorChoices101: Inspector choices that apply when the selected objects differ (plugdata PR 7)

**The links expect** `~/Projects/plugdata-prs/presentation-objects` to be checked out on `pr/gui-fixes` (it is). That branch holds all five of my GUI-fix PRs (4 to 8) together. `Inspector.h` is identical there to this PR's own branch, `pr/fix-inspector-mixed-choices`, so checking that one out instead gives the same lines.

## What this PR is, in plain words

**In short.** Select two objects that differ in a drop-down (one knob "Always", one "Never"). The Inspector showed the first one's value, and picking that value did nothing. Now it shows empty, so any pick applies to both.

**What you run into without it.** Select several objects and the Inspector shows the settings they have in common, so you can change them all at once. Take a drop-down (a "choice") where the objects disagree: one knob has "Show number" set to Always, another to Never. The Inspector shows the first object's choice, Always. Pick "Always" to apply it to both, and nothing happens: the second knob still shows no number. I reproduced it in upstream plugdata with exactly those two knobs.

**Why.** For several objects, the Inspector shows one shared value, and when that value changes, it copies the new value into every selected object. A change is only noticed when the value really changes. The shared value starts as the first object's value, so picking that same choice changes nothing, and nothing is copied.

**What the PR changes.** When the selected objects don't share a choice, the drop-down starts empty. Then any choice you pick is a change, and it's applied to all of them. Only drop-downs change; numbers, colours and yes/no switches behave as before. One file, `Source/Sidebar/Inspector.h`: 17 lines added, 5 removed. It's Inspector code, the same on every platform.

**What it needs first.** Nothing.

## How it came about

**Who ran into it, doing what.** On 9 November 2026, after installing that day's PgD4S build, you tried to set Flechtwerk's bottom FM knob back to "Knob" in the Inspector, and it stayed a needle. The coordinator passed it on as: "The user tried on the bottom FM knob, chose Knob, and it stayed a needle."

**What caused it there.** Two things. The main one was PgD4S-only: the style was saved, but the knob was never redrawn. While checking, the objects agent found a second way to hit it (12:19): "one way to hit the bug is changing the Mods and FM together". With several knobs selected whose styles differ, the Inspector shows the first knob's style, and picking that style does nothing. That second cause is in upstream plugdata too, for every drop-down.

**The first fix, in PgD4S.** Part of commit f7eb24012 (9 November, 12:51): "Inspector: a choice the selected objects don't share shows as none, so picking it applies."

**From PgD4S to this PR.** The PR is the Inspector part only, unchanged. The redraw fix belongs to PgD4S's knob styles and stays there. I showed the problem in upstream with a setting upstream has: two knobs with "Show number" on Always and Never.

## How the Inspector edits several objects (unchanged code)

- [Inspector.h:30-60](../../plugdata-prs/presentation-objects/Source/Sidebar/Inspector.h#L30-L60) is a `Property`: one setting shared by the selected objects. `values` holds each object's own copy of the setting, and `baseValue` is the one value the Inspector's drop-down shows and edits.
- [Inspector.h:62-90](../../plugdata-prs/presentation-objects/Source/Sidebar/Inspector.h#L62-L90) is what runs when `baseValue` changes: it writes the new value into every object's copy (lines 80-82), inside one undo step.
- [PropertiesPanel.cpp:186-188](../../plugdata-prs/presentation-objects/Source/Components/PropertiesPanel.cpp#L186-L188) is the drop-down itself. Its items are numbered from 1, and it shows the item whose number is in the value it's tied to. A value of 0 matches no item, so the drop-down shows nothing.

## The code, in reading order

1. **[Inspector.h:18-22](../../plugdata-prs/presentation-objects/Source/Sidebar/Inspector.h#L18-L22)** is `addProperty`, which makes one shared setting. It gets a new last argument, `mixedShowsNothing`, which it passes on to the `Property` it makes. It's `false` unless the caller asks for it, so nothing else that calls `addProperty` changes. See "An argument with a default value".
   Python: `def add_property(self, controller_value, attached_values, mixed_shows_nothing=False): ...`.
2. **[Inspector.h:31-35](../../plugdata-prs/presentation-objects/Source/Sidebar/Inspector.h#L31-L35)** is the start of the `Property`'s constructor, the code that runs when one is made. Line 31 takes the new argument; the rest is unchanged and keeps the parent and every object's copy of the setting. See "A constructor's first lines".
   Python: `def __init__(self, parent, controller_value, attached_values, mixed_shows_nothing): self.redirector = parent; self.values = attached_values + [controller_value]`.
3. **[Inspector.h:37-48](../../plugdata-prs/presentation-objects/Source/Sidebar/Inspector.h#L37-L48)** chooses what the drop-down first shows.
   - It starts from the first object's value, as before.
   - If `mixedShowsNothing` is on, it goes through every selected object's value, and if any differs, it uses 0 instead and stops looking.
   - Line 48 sets that as the shared value. Before, line 48 always used the first object's value.

   Python: `shown = controller_value.get()` and `if mixed_shows_nothing and any(v.get() != shown for v in self.values): shown = 0` and `self.base_value.set(shown)`.
4. **[Inspector.h:267](../../plugdata-prs/presentation-objects/Source/Sidebar/Inspector.h#L267)** is where the Inspector makes a shared setting for several selected objects. It now asks for `mixedShowsNothing` only when the setting is a drop-down (`type == tCombo`). Other kinds keep their old behaviour: a number box shows the first object's number, and typing a new one still applies to all.
   Python: `prop = redirector.add_property(value, other_values, mixed_shows_nothing=(type == T_COMBO))`.

Block 3 is the actual fix. Blocks 1 and 2 carry the new option through, and block 4 turns it on for drop-downs only.

## Why the same value changes nothing (block 3)

`baseValue` is a JUCE `Value`: a value that tells its listeners when it changes. Setting it to the value it already has tells no one, so the code at lines 62-90 never runs. That's sensible in general, since nothing changed, but here the shared value isn't really every object's value: it's only the first one's. Starting at 0 makes any pick a real change.

Python comparison: a property setter that returns early when nothing changes:

```python
@value.setter
def value(self, new):
    if new == self._value:
        return            # listeners never hear about it
    self._value = new
    for listener in self.listeners:
        listener(new)
```

## A loop over a list, and `auto const*` (block 3)

```cpp
                    for (auto const* value : values) {
                        if (value->getValue() != shown) {
                            shown = 0;
                            break;
                        }
                    }
```

`for (x : list)` is Python's `for x in list:`. `values` holds addresses of each object's setting, so `value` is an address: `auto const*` means "the address of something, whose type the compiler works out, that this loop won't change". `value->getValue()` reads through that address. See [saveCanvas101: A struct, and what the stars mean](saveCanvas101.md#a-struct-and-what-the-stars-mean-block-1). `break` leaves the loop, as in Python.

`shown` holds a JUCE `var`, which can hold a number, a text or other things, like a Python variable. `shown = 0` puts the number 0 in it.

## An argument with a default value (block 1)

```cpp
        Value* addProperty(Value* controllerValue, SmallArray<Value*> const& attachedValues, bool const mixedShowsNothing = false)
```

`= false` gives the last argument a default, so existing calls with two arguments still work. `Value*` (an address of a `Value`) is what the function returns: the shared value, for the drop-down to tie itself to. `SmallArray<Value*> const&` is a list of addresses, passed without copying it (`&`) and without changing it (`const`).

Python: `def add_property(self, controller_value, attached_values, mixed_shows_nothing=False) -> Value:`.

## A constructor's first lines (block 2)

```cpp
            Property(PropertyRedirector* parent, Value* controllerValue, SmallArray<Value*> const& attachedValues, bool const mixedShowsNothing)
                : redirector(parent)
                , values(attachedValues)
            {
```

A function with the class's own name is its constructor, Python's `__init__`. The lines after the `:` fill the object's fields before the body runs. Python does the same in the body: `self.redirector = parent` and `self.values = list(attached_values)`.
