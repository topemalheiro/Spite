# Fix Inspector choices not applying when selected objects differ

[← Overview](README.md) · **PR:** [plugdata-team/plugdata#2428](https://github.com/plugdata-team/plugdata/pull/2428) · plugdata `develop` ← `topemalheiro:pr/fix-inspector-mixed-choices` · 1 commit, +17 −5 in `Source/Sidebar/Inspector.h` · needs nothing first

## What it does

**In short.** Select two objects that differ in a drop-down (one knob "Always", one "Never"). The Inspector showed the first one's value, and picking that value did nothing. Now it shows empty, so any pick applies to both.

**What you run into without it.** Take a drop-down where the selected objects disagree: one knob has "Show number" set to Always, another to Never. The Inspector shows the first object's choice, Always. Pick "Always" to apply it to both, and nothing happens: the second knob still shows no number. I reproduced it on `develop` with exactly those two knobs.

**Why.** For several objects, the Inspector shows one shared value, and when that value changes, it copies the new value into every selected object. The shared value starts as the first object's value, so picking that same choice isn't a change, and nothing is copied.

**What changes.** When the selected objects don't share a choice, the drop-down starts empty. Then any choice you pick is a change, and it's applied to all of them. Only drop-downs change; numbers, colours and yes/no switches behave as before. It's Inspector code, the same on every platform.

## How the Inspector edits several objects (unchanged code)

- [Inspector.h:30-60](https://github.com/topemalheiro/plugdata/blob/b67b7b0140f126d016dd84767e1f3d4f915c6d55/Source/Sidebar/Inspector.h#L30-L60) is a `Property`: one setting shared by the selected objects. `values` holds each object's own copy of the setting, and `baseValue` is the one value the Inspector's drop-down shows and edits.
- [Inspector.h:62-90](https://github.com/topemalheiro/plugdata/blob/b67b7b0140f126d016dd84767e1f3d4f915c6d55/Source/Sidebar/Inspector.h#L62-L90) is what runs when `baseValue` changes: it writes the new value into every object's copy (lines 80-82), inside one undo step.
- [PropertiesPanel.cpp:186-188](https://github.com/topemalheiro/plugdata/blob/b67b7b0140f126d016dd84767e1f3d4f915c6d55/Source/Components/PropertiesPanel.cpp#L186-L188) is the drop-down itself. Its items are numbered from 1, and it shows the item whose number is in the value it's tied to. A value of 0 matches no item, so the drop-down shows nothing.

## The code, in reading order

1. **[Inspector.h:18-22](https://github.com/topemalheiro/plugdata/blob/b67b7b0140f126d016dd84767e1f3d4f915c6d55/Source/Sidebar/Inspector.h#L18-L22)** is `addProperty`, which makes one shared setting. It gets a new last argument, `mixedShowsNothing`, which it passes on to the `Property` it makes. It defaults to `false`, so nothing else that calls `addProperty` changes.
2. **[Inspector.h:31-35](https://github.com/topemalheiro/plugdata/blob/b67b7b0140f126d016dd84767e1f3d4f915c6d55/Source/Sidebar/Inspector.h#L31-L35)** is the start of the `Property`'s constructor. Line 31 takes the new argument; the rest is unchanged.
3. **[Inspector.h:37-48](https://github.com/topemalheiro/plugdata/blob/b67b7b0140f126d016dd84767e1f3d4f915c6d55/Source/Sidebar/Inspector.h#L37-L48)** chooses what the drop-down first shows.
   - It starts from the first object's value, as before.
   - If `mixedShowsNothing` is on, it goes through every selected object's value, and if any differs, it uses 0 instead and stops looking.
   - Line 48 sets that as the shared value. Before, line 48 always used the first object's value.
4. **[Inspector.h:267](https://github.com/topemalheiro/plugdata/blob/b67b7b0140f126d016dd84767e1f3d4f915c6d55/Source/Sidebar/Inspector.h#L267)** is where the Inspector makes a shared setting for several selected objects. It now asks for `mixedShowsNothing` only when the setting is a drop-down (`type == tCombo`). Other kinds keep their old behaviour: a number box shows the first object's number, and typing a new one still applies to all.

Block 3 is the actual fix. Blocks 1 and 2 carry the new option through, and block 4 turns it on for drop-downs only.

How it was tested, and what wasn't, is in the PR description.
