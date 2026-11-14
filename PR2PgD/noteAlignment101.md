# noteAlignment101: centred and right-aligned text in fixed-width notes (plugdata PR 6)

**The links expect** `~/Projects/plugdata-prs/presentation-objects` to be checked out on `pr/gui-fixes` (it is). That branch holds all five of my GUI-fix PRs (4 to 8) together. `NoteObject.h` is identical there to this PR's own branch, `pr/fix-note-fixed-width-alignment`, so checking that one out instead gives the same lines. The link into `Libraries/pd-else` opens ELSE at the version upstream plugdata uses.

## What this PR is, in plain words

**In short.** Only for a note you've resized to a fixed width. Centred text sat a bit right of centre, and right-aligned text ran past the box. Now they line up with the box. Left-aligned notes don't change.

**What you run into without it.** `[note]` is ELSE's comment box, with its own font, colours, outline and alignment. Once you drag a note's edge, it keeps that width and wraps its text inside it: a "fixed-width" note. If such a note is centred, its text sits about 6 px right of the middle of the box. If it's right-aligned, the text runs past the box's right edge. It shows clearly with "Outline" or "Fill background" on. I reproduced it in upstream plugdata with three 170 px notes, one left-aligned, one centred and one right-aligned, each with an outline and a background.

**Why.** plugdata shows a note's text in a JUCE text editor placed over the box. The editor is 6 px wider than the box, and its text starts 7 px in from the left with no gap on the right. For left-aligned text that's fine. But centred text is centred on the area from 7 px in to 6 px past the box, which is about 6 px right of the box's middle, and right-aligned text ends 6 px past the box. In Pd itself, ELSE moves such text so it's centred on the box or ends at its right edge ([note.c:194-207](../../plugdata-prs/presentation-objects/Libraries/pd-else/Source/Control/note.c#L194-L207)).

**What the PR changes.** A fixed-width note that's centred or right-aligned now lays out its text in exactly the box's width, with an even 2 px gap on each side. That leaves its text 3 px less room than before, so a line can wrap a word earlier (see "The numbers, before and after"). Its height is measured the same way, so the box still fits the text, and changing the alignment refits the text at once. Left-aligned notes, and notes that size themselves to their text, are laid out exactly as before. One file, `Source/Objects/NoteObject.h`: 32 lines added, 4 removed. It's layout code, the same on every platform.

**What it needs first.** Nothing.

## How it came about

**Who ran into it, doing what.** On 9 November 2026 you compared the converted Flechtwerk device in PgD4S with the Max original. The converter turns Max's comments into `[note]`s. Looking at one in the Inspector, you said: "justification centered, bro. Bro, this shit isn't even centering correctly", and that notes can stand in for comments, but not "centered wrong".

**What caused it there.** Max comments have a fixed width, and many sit centred or right-aligned next to a dial. The converter gives each `[note]` that width and alignment, so every converted device showed the 6 px shift. In a hand-made patch you only meet it after you resize a note and centre it.

**The first fix, in PgD4S.** Part of commit 1b4f6a117 (9 November, 03:26), by the objects agent.

**From PgD4S to this PR.** PgD4S's version changes every fixed-width note, and it lets a word that's too long for the box stick out evenly on both sides instead of breaking it, as Max does. The PR keeps only what vanilla Pd does too: centred and right-aligned text line up with the box. Left-aligned notes stay as upstream has them, so nobody's existing notes wrap differently. I showed the problem without Max devices, with three plain 170 px notes in upstream plugdata.

## The code, in reading order

First three small helper functions, then the places that use them.

1. **[NoteObject.h:235-237](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L235-L237)** is a comment saying which notes get the new layout and why.
   Python: a `#` comment.
2. **[NoteObject.h:238-241](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L238-L241)** answers "does this note get the new layout?": yes when it has a fixed width and isn't left-aligned. `justification` is the Inspector's setting, numbered as in its list: 1 Left, 2 Centred, 3 Right.
   Python: `def aligns_in_fixed_width(self, fixed_width): return fixed_width and self.justification != 1`.
3. **[NoteObject.h:243-248](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L243-L248)** chooses the gap around the text: 2 px left and right for those notes, the usual 7 px on the left and none on the right for the others. It only changes the editor's border when the border is different, because changing it makes the editor lay out its text again. See "A border written as four numbers".
   Python: `border = Border(1, 2, 1, 2) if self.aligns_in_fixed_width(fixed) else self.border` and `if editor.border != border: editor.border = border`.
4. **[NoteObject.h:250-253](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L250-L253)** chooses the editor's width: exactly the box's width for those notes, the box plus 6 px for the others, as before.
   Python: `def editor_width(self, box_width, fixed): return box_width if self.aligns_in_fixed_width(fixed) else box_width + 6`.
5. **[NoteObject.h:255-263](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L255-L263)** is `resized()`, which JUCE calls whenever the note's box changes size. It reads whether the note has a fixed width from the note's Pd data (`x_resized`: set when you drag the note's edge or type a width in the Inspector, at [NoteObject.h:341](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L341) and [383](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L383), and saved with the patch), then sets the editor's border and width with blocks 3 and 4. Before, it was one line that always made the editor 6 px wider than the box: `noteEditor.setBounds(getLocalBounds().withTrimmedRight(-6));`. See "Functions the framework calls" and "Reading Pd's data under its lock".
   Python: `def resized(self): fixed = note.x_resized; self.update_editor_border(fixed); self.editor.set_bounds(self.local_bounds().with_width(self.editor_width(self.width, fixed)))`.
6. **[NoteObject.h:287-292](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L287-L292)** is the start of `getTextHeightForWidth`, which measures how tall the text is at a given width: it sets the editor to that width for a moment, asks for the text's height, and puts it back. It now takes the fixed-width flag too and measures with the same border and width that will be shown (blocks 3 and 4), so the box is exactly as tall as its text.
   Python: `def text_height_for_width(self, width, fixed): self.update_editor_border(fixed); old = editor.bounds; editor.bounds = (old.x, old.y, self.editor_width(width, fixed), 1); h = editor.text_height(); editor.bounds = old; return h`.
7. **[NoteObject.h:306](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L306)** is in `getPdBounds`, where plugdata asks for the note's position and size. It passes the fixed-width flag on to block 6.
   Python: `return Rect(note.x, note.y, width, self.text_height_for_width(width, note.x_resized) + 2)`.
8. **[NoteObject.h:441](../../plugdata-prs/presentation-objects/Source/Objects/NoteObject.h#L441)** runs when you change the alignment in the Inspector. It calls `resized()` by hand, because the box keeps its size, so JUCE wouldn't call it, and the editor would keep the old width until the next resize.
   Python: `self.resized()`.

Blocks 3 to 5 are the actual fix: the even gap and the box's own width, applied whenever the note is sized. Block 2 decides which notes get it, blocks 6 and 7 keep the height right, and block 8 applies it straight away when the alignment changes.

## The numbers, before and after

Take a fixed-width note `W` px wide.

- **Before:** the editor runs from 0 to `W + 6`, and its text area from 7 to `W + 6`. Centred text is centred at `(7 + W + 6) / 2 = W/2 + 6.5`, so about 6 px right of the box's middle. Right-aligned text ends at `W + 6`, past the box.
- **After (centred or right-aligned):** the editor runs from 0 to `W`, and its text area from 2 to `W - 2`. Centred text is centred at `W/2`, and right-aligned text ends 2 px inside the box. The text area is now `W - 4` wide instead of `W - 1`, 3 px narrower, so a line that only just fitted before can wrap a word earlier.
- **Left-aligned, or sized to its text:** unchanged, 7 to `W + 6`. Keeping that width means the lines wrap exactly where they did before.

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

JUCE, the library plugdata's interface is built on, has a `resized()` in every component that does nothing. A component that cares about its size writes its own, and `override` says it replaces the one it inherits. JUCE then calls it whenever the size changes; plugdata's own code never needs to, except where the size stays the same but the layout must still change (block 8).

Python: overriding a method the framework calls, like `def resizeEvent(self, event):` in a Qt widget.

## `const` after a function's name (blocks 2 and 4)

```cpp
    bool alignsInFixedWidth(bool const fixedWidth) const
```

The `const` at the end promises the function only reads the note and changes nothing in it; the compiler refuses to build it otherwise. The `const` in `bool const fixedWidth` says the argument isn't changed inside the function. `updateEditorBorder` (block 3) has no `const` at the end, because it changes the editor.

Python has no such promise. The nearest is a naming habit, or a `@property` that only returns a value.

## Reading Pd's data under its lock (block 5)

```cpp
        bool fixedWidth = false;
        if (auto note = ptr.get<t_fake_note>())
            fixedWidth = note->x_resized;
```

`ptr.get<t_fake_note>()` takes Pd's lock and gives the note's Pd data, or nothing if the note has been deleted. The lock is let go at the end of the `if`. The other agent's note explains this pattern: [hostStalls101: A lock held for one block](hostStalls101.md#a-lock-held-for-one-block-blocks-3-and-7). `note->x_resized` reads a field through an address; see [saveCanvas101: A struct, and what the stars mean](saveCanvas101.md#a-struct-and-what-the-stars-mean-block-1). The value is copied out, so the rest of `resized()` runs without the lock.

Python: `with pd_lock: fixed_width = note.x_resized if note else False`.
