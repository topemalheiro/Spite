# Fix centred and right-aligned text in fixed-width notes

[← Overview](README.md) · **PR:** [plugdata-team/plugdata#2427](https://github.com/plugdata-team/plugdata/pull/2427) · plugdata `develop` ← `topemalheiro:pr/fix-note-fixed-width-alignment` · 1 commit, +32 −4 in `Source/Objects/NoteObject.h` · needs nothing first

## What it does

**In short.** Only for a `[note]` that has been resized to a fixed width. Centred text sat a bit right of centre, and right-aligned text ran past the box. Now they line up with the box. Left-aligned notes don't change.

![Left-aligned, centred and right-aligned notes of fixed width, before and after](images/note-alignment-before-after.png)

**What you run into without it.** Once you drag a note's edge, it keeps that width and wraps its text inside it. If such a note is centred, its text sits about 6 px right of the middle of the box. If it's right-aligned, the text runs past the box's right edge. It shows clearly with "Outline" or "Fill background" on. I reproduced it on `develop` with three 170 px notes, one left-aligned, one centred and one right-aligned, each with an outline and a background.

**Why.** The note's text editor is 6 px wider than the box, and its text starts 7 px in from the left with no gap on the right. For left-aligned text that's fine. But centred text is centred on the area from 7 px in to 6 px past the box, which is about 6 px right of the box's middle, and right-aligned text ends 6 px past the box. In Pd itself, ELSE moves such text so it's centred on the box or ends at its right edge ([note.c:194-207](https://github.com/timothyschoen/pd-else/blob/f92f4c83242d075526191d85b96eb5c8e10a0944/Source/Control/note.c#L194-L207)).

**What changes.** A fixed-width note that's centred or right-aligned now lays out its text in exactly the box's width, with an even 2 px gap on each side. Its height is measured the same way, so the box still fits the text, and changing the alignment refits the text at once. The text area of such a note is 3 px narrower than before, so a line that only just fit now wraps a word earlier. Left-aligned notes, and notes that size themselves to their text, are laid out exactly as before. It's layout code, the same on every platform.

## The code, in reading order

First three small helper functions, then the places that use them.

1. **[NoteObject.h:235-237](https://github.com/topemalheiro/plugdata/blob/8aae7ef6e1495d0a686aa37d79d4dde51d52a225/Source/Objects/NoteObject.h#L235-L237)** is a comment saying which notes get the new layout and why.
2. **[NoteObject.h:238-241](https://github.com/topemalheiro/plugdata/blob/8aae7ef6e1495d0a686aa37d79d4dde51d52a225/Source/Objects/NoteObject.h#L238-L241)** answers "does this note get the new layout?": yes when it has a fixed width and isn't left-aligned. `justification` is the Inspector's setting, numbered as in its list: 1 Left, 2 Centred, 3 Right.
3. **[NoteObject.h:243-248](https://github.com/topemalheiro/plugdata/blob/8aae7ef6e1495d0a686aa37d79d4dde51d52a225/Source/Objects/NoteObject.h#L243-L248)** chooses the gap around the text: 2 px left and right for those notes, the usual 7 px on the left and none on the right for the others. It only changes the editor's border when the border is different, because changing it makes the editor lay out its text again.
4. **[NoteObject.h:250-253](https://github.com/topemalheiro/plugdata/blob/8aae7ef6e1495d0a686aa37d79d4dde51d52a225/Source/Objects/NoteObject.h#L250-L253)** chooses the editor's width: exactly the box's width for those notes, the box plus 6 px for the others, as before.
5. **[NoteObject.h:255-263](https://github.com/topemalheiro/plugdata/blob/8aae7ef6e1495d0a686aa37d79d4dde51d52a225/Source/Objects/NoteObject.h#L255-L263)** is `resized()`. It reads whether the note has a fixed width from the note's Pd data (`x_resized`: set when you drag the note's edge or type a width in the Inspector, at [NoteObject.h:341](https://github.com/topemalheiro/plugdata/blob/8aae7ef6e1495d0a686aa37d79d4dde51d52a225/Source/Objects/NoteObject.h#L341) and [383](https://github.com/topemalheiro/plugdata/blob/8aae7ef6e1495d0a686aa37d79d4dde51d52a225/Source/Objects/NoteObject.h#L383), and saved with the patch), then sets the editor's border and width with blocks 3 and 4. Before, it was one line that always made the editor 6 px wider than the box: `noteEditor.setBounds(getLocalBounds().withTrimmedRight(-6));`.
6. **[NoteObject.h:287-292](https://github.com/topemalheiro/plugdata/blob/8aae7ef6e1495d0a686aa37d79d4dde51d52a225/Source/Objects/NoteObject.h#L287-L292)** is the start of `getTextHeightForWidth`, which measures how tall the text is at a given width: it sets the editor to that width for a moment, asks for the text's height, and puts it back. It now takes the fixed-width flag too and measures with the same border and width that will be shown (blocks 3 and 4), so the box is exactly as tall as its text.
7. **[NoteObject.h:306](https://github.com/topemalheiro/plugdata/blob/8aae7ef6e1495d0a686aa37d79d4dde51d52a225/Source/Objects/NoteObject.h#L306)** is in `getPdBounds`. It passes the fixed-width flag on to block 6.
8. **[NoteObject.h:441](https://github.com/topemalheiro/plugdata/blob/8aae7ef6e1495d0a686aa37d79d4dde51d52a225/Source/Objects/NoteObject.h#L441)** runs when the alignment changes in the Inspector. It calls `resized()` by hand, because the box keeps its size, so JUCE wouldn't call it, and the editor would keep the old width until the next resize.

Blocks 3 to 5 are the actual fix: the even gap and the box's own width, applied whenever the note is sized. Block 2 decides which notes get it, blocks 6 and 7 keep the height right, and block 8 applies it straight away when the alignment changes.

## The numbers, before and after

Take a fixed-width note `W` px wide.

- **Before:** the editor runs from 0 to `W + 6`, and its text area from 7 to `W + 6`. Centred text is centred at `(7 + W + 6) / 2 = W/2 + 6.5`, so about 6 px right of the box's middle. Right-aligned text ends at `W + 6`, past the box.
- **After (centred or right-aligned):** the editor runs from 0 to `W`, and its text area from 2 to `W - 2`. Centred text is centred at `W/2`, and right-aligned text ends 2 px inside the box.
- **Left-aligned, or sized to its text:** unchanged, 7 to `W + 6`. Keeping that width means the lines wrap exactly where they did before.

How it was tested, and what wasn't, is in the PR description.
