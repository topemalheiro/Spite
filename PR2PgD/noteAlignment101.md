# noteAlignment101: centred and right-aligned text in notes (plugdata PR 6, #2427)

**The links expect** `~/Projects/plugdata-prs/presentation-objects` to be checked out on `pr/gui-fixes` (it is). That branch holds all five of my GUI-fix PRs (4 to 8) together, including both commits of this one. `NoteObject.h` is identical there to this PR's own branch, `pr/fix-note-fixed-width-alignment`, so checking that one out instead gives the same lines. The link into `Libraries/pd-else` opens ELSE at the version upstream plugdata uses.

## What this PR is, in plain words

**In short.** For a note that's centred or right-aligned: centred text sat a bit right of the middle, and right-aligned text ran past the box. Now the text lines up with the box, whether you've resized the note or it sizes itself to its text. Left-aligned notes don't change.

**What you run into without it.** `[note]` is ELSE's comment box, with its own font, colours, outline and alignment. It comes in two kinds:
- **Sized to its text:** a new note's box is as wide as its text (plus 12 px), on one line.
- **Fixed width:** once you drag a note's edge, it keeps that width and wraps its text inside it.

In both kinds, a centred note's text sits about 6 px right of the middle of the box, and a right-aligned note's text runs past the box's right edge. It shows clearly with "Outline" or "Fill background" on, or in edit mode, which draws a faint box around each note. I reproduced it in upstream plugdata with three 170 px notes, and Timothy with a note that he never resized ("lorem ipsum dolor", right-aligned).

**Why.** plugdata shows a note's text in a JUCE text editor placed over the box. The editor is 6 px wider than the box, and its text starts 7 px in from the left with no gap on the right. For left-aligned text that's fine. But centred text is centred on the area from 7 px in to 6 px past the box, which is about 6 px right of the box's middle, and right-aligned text ends 6 px past the box. In Pd itself, ELSE moves such text so it's centred on the box or ends at its right edge ([note.c:194-207](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/note.c#L194-L207)). A note sized to its text needs no moving there, because its box fits the text exactly.

**What the PR changes.** A centred or right-aligned note now lays out its text in exactly the box's width, with an even 2 px gap on each side. Its height is measured the same way, so the box still fits the text, and changing the alignment refits the text at once.
- **A resized note:** its text gets 3 px less room than before, so a line can wrap a word earlier.
- **A note sized to its text:** its box stays text + 12 px wide, which still fits the text on one line.

Left-aligned notes are laid out exactly as before. One file, `Source/Objects/NoteObject.h`: 26 lines added, 2 removed, in two commits. The first covered resized notes only, and the second (after Timothy's test) covers every centred or right-aligned note. It's layout code, the same on every platform.

**What it needs first.** Nothing.

## How it came about

**Who ran into it, doing what.** On 9 November 2026 you compared the converted Flechtwerk device in PgD4S with the Max original. The converter turns Max's comments into `[note]`s. Looking at one in the Inspector, you said: "justification centered, bro. Bro, this shit isn't even centering correctly", and that notes can stand in for comments, but not "centered wrong".

**What caused it there.** Max comments have a fixed width, and many sit centred or right-aligned next to a dial. The converter gives each `[note]` that width and alignment, so every converted device showed the 6 px shift.

**The first fix, in PgD4S.** Part of commit 1b4f6a117 (9 November, 03:26), by the objects agent.

**From PgD4S to this PR.** PgD4S's version changes every fixed-width note, and it lets a word that's too long for the box stick out evenly on both sides instead of breaking it, as Max does. The PR keeps only what vanilla Pd does too: centred and right-aligned text line up with the box. Left-aligned notes stay as upstream has them, so nobody's existing notes wrap differently. I showed the problem without Max devices, with three plain 170 px notes in upstream plugdata.

**The second commit, after Timothy's test.** On 15 November Timothy, plugdata's maintainer, tested the PR and wrote on Discord: "if I don't resize it and set it to right-justified text seems to go past the object bounds. Maybe that was already the case before too?" It was: a note sized to its text has the same 6 px-wider editor, and the first commit had left those notes alone on purpose. The second commit (2b308344f) drops the "only resized notes" condition, which also made the code shorter.

## The code, in reading order

First three small helper functions, then the places that use them. The links show the code with both commits in.

1. **[NoteObject.h:235-237](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L235-L237)** is a comment saying which notes get the new layout and why.
   Python: a `#` comment.
2. **[NoteObject.h:238-241](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L238-L241)** answers "does this note get the new layout?": yes when it isn't left-aligned. `justification` is the Inspector's setting, numbered as in its list: 1 Left, 2 Centred, 3 Right.
   Python: `def aligns_in_box(self): return self.justification != 1`.
3. **[NoteObject.h:243-248](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L243-L248)** chooses the gap around the text: 2 px left and right for those notes, the usual 7 px on the left and none on the right for the others. It only changes the editor's border when the border is different, because changing it makes the editor lay out its text again. See "A border written as four numbers".
   Python: `border = Border(1, 2, 1, 2) if self.aligns_in_box() else self.border`, then `if editor.border != border: editor.border = border`.
4. **[NoteObject.h:250-253](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L250-L253)** chooses the editor's width: exactly the box's width for those notes, the box plus 6 px for the others, as before.
   Python: `def editor_width(self, box_width): return box_width if self.aligns_in_box() else box_width + 6`.
5. **[NoteObject.h:255-259](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L255-L259)** is `resized()`, which JUCE calls whenever the note's box changes size. It sets the editor's border and width with blocks 3 and 4. Before, it was one line that always made the editor 6 px wider than the box: `noteEditor.setBounds(getLocalBounds().withTrimmedRight(-6));`. See "Functions the framework calls".
   Python: `def resized(self): self.update_editor_border(); self.editor.set_bounds(self.local_bounds().with_width(self.editor_width(self.width)))`.
6. **[NoteObject.h:285-288](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L285-L288)** is in `getTextHeightForWidth`, which measures how tall the text is at a given width: it sets the editor to that width for a moment, asks for the text's height, and puts it back. It now measures with the same border and width that will be shown (blocks 3 and 4), so the box is exactly as tall as its text. Line 287 is unchanged.
   Python: `self.update_editor_border(); old = editor.bounds; editor.bounds = (old.x, old.y, self.editor_width(width), 1)`.
7. **[NoteObject.h:437](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L437)** runs when you change the alignment in the Inspector. It calls `resized()` by hand, because the box keeps its size, so JUCE wouldn't call it, and the editor would keep the old width until the next resize.
   Python: `self.resized()`.

Blocks 3 to 5 are the actual fix: the even gap and the box's own width, applied whenever the note is sized. Block 2 decides which notes get it, block 6 keeps the height right, and block 7 applies it straight away when the alignment changes.

## The numbers, before and after

**A resized note `W` px wide.**
- **Before:** the editor runs from 0 to `W + 6`, and its text area from 7 to `W + 6`. Centred text is centred at `(7 + W + 6) / 2 = W/2 + 6.5`, so about 6 px right of the box's middle. Right-aligned text ends at `W + 6`, past the box.
- **After (centred or right-aligned):** the editor runs from 0 to `W`, and its text area from 2 to `W - 2`. Centred text is centred at `W/2`, and right-aligned text ends 2 px inside the box. The text area is now `W - 4` wide instead of `W - 1`, 3 px narrower, so a line that only just fitted before can wrap a word earlier.

**A note sized to its text, `t` px wide.** Its box is `W = t + 12` wide ([NoteObject.h:297](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L297), unchanged).
- **Before:** as above, so right-aligned text ended 6 px past the box.
- **After (centred or right-aligned):** the text area is `t + 8` wide, enough for the text on one line. Centred text has 6 px on each side. Right-aligned text ends a few pixels inside the box: 2 px of border plus the 2 px JUCE keeps for the text cursor. In the sandbox, every such note stayed on one line, at the same height as before: 10 to 30 pt, bold and italic, and a row of W's.

**Left-aligned notes, of either kind:** unchanged, 7 to `W + 6`. Keeping that width means the lines wrap exactly where they did before. In the sandbox they're the same pixel for pixel as on develop.

## A border written as four numbers (block 3)

```cpp
    BorderSize<int> border { 1, 7, 1, 0 };
```

That's [NoteObject.h:15](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L15), the gap the editor keeps inside its edges, in JUCE's order: top, left, bottom, right. So 1 px top and bottom, 7 px left, none on the right. Block 3's `BorderSize<int>(1, 2, 1, 2)` is 2 px on both sides. The `<int>` says the gaps are whole numbers.

Python: `Border = namedtuple("Border", "top left bottom right")`, then `Border(1, 7, 1, 0)`.

The old line's `withTrimmedRight(-6)` trims a negative amount off the right, which adds 6 px: that's where the editor's extra width came from.

## Functions the framework calls (block 5)

```cpp
    void resized() override
```

JUCE, the library plugdata's interface is built on, has a `resized()` in every component that does nothing. A component that cares about its size writes its own, and `override` says it replaces the one it inherits. JUCE then calls it whenever the size changes. plugdata's own code never needs to, except where the size stays the same but the layout must still change (block 7).

Python: overriding a method the framework calls, like `def resizeEvent(self, event):` in a Qt widget.

## `const` after a function's name (blocks 2 and 4)

```cpp
    bool alignsInBox() const
```

```cpp
    int getEditorWidth(int const boxWidth) const
```

The `const` at the end promises the function only reads the note and changes nothing in it; the compiler refuses to build it otherwise. The `const` in `int const boxWidth` says the argument isn't changed inside the function. `updateEditorBorder` (block 3) has no `const` at the end, because it changes the editor.

Python has no such promise. The nearest is a naming habit, or a `@property` that only returns a value.

## Questions that came up

**Timothy: "if I don't resize it and set it to right-justified text seems to go past the object bounds. Maybe that was already the case before too?"** Yes, upstream develop does the same. The first commit only changed notes you've resized, and said so. The second commit applies the same layout to every centred or right-aligned note, so his note's text now stays inside its box.
