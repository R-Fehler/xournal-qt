# Snip: a picture of part of a page (`qt/snip`)

The author (2026-10-04): "a very fast/accessible select area and screenshot tool … reuse the logic of lasso or
rectangle selection to quickly copy the pixels of the canvas in the selection area to the system clipboard … it
should allow very fast copy pasting of one document to another … when inserted into another document of xournal-qt,
ask the user whether they also want to paste a link to the source document and page besides the pasted image."

## Using it

- **Where:** with the toolbox, a **fixed tool of the rail** after select (qt/copy-tools, the author: "Snip should be
  one click away … not hidden behind the normal select tool"): its icon is its shape, a tap snips, a tap while it is
  armed takes the other shape (remembered); held: both shapes and the resolution. (The classic tool bar had them in the
  select button's list until 0.8.0.) Everywhere: the image button's list (press and hold, or right-click): "Snip from a page (copy a picture)" and "Snip
  with the lasso" (a tap on it still opens the file picker); the keys **Shift+S** (rectangle) and **Shift+L** (lasso),
  changeable in the shortcut sheet; a **Snip** entry of one's own in the toolbox ("+" → Snip, qt/ui-rework). A tap on
  the select button still goes rectangle ↔ lasso, and a snip is never remembered as the select button's variant.
- **One snip per activation**, as screenshot tools do: the select tool of that shape is taken, the next rectangle or
  lasso dragged over a page is copied, and the tool in hand before comes back. "Copied picture" says it worked. A tap
  is no snip (it stays armed); Escape, the select button, a key or any other tool ends it (Escape gives the tool
  before back; another tool stays).
- **Everywhere it can read:** the notes, the reference beside them (also while it is only for reading), a Markdown or
  text document (the press snips there instead of putting the cursor into the text), a document opened read-only.
  A press over a sticky note snips too (it does not take the note).

## The picture

- What the page shows there: the background (paper, ruling, the PDF page, a background image) and every visible
  layer: ink, text, images, Markdown boxes, sticky notes (a covering note that peeks is drawn as it peeks). Never what
  lies over the page on the screen only: the curtain and the spotlight, the hover pointer, a selection and its handles,
  the rectangle or lasso being drawn, the setsquare.
- **Resolution:** a setting, chosen once and not asked with every snip (the author, 2026-10-05: "it should only be
  adjusted, not asked every time the user does a snip"): Settings → Documents → "Pictures copied to the clipboard" →
  Snip, the snip button's list (held), and a snip entry's editor (`snipResolution`: `screen`, `high`, `veryHigh`):
  - **As sharp as the screen** (the default, as before): the screen's (zoom × the screen's pixel ratio) but at least
    200 dpi (`region::MIN_DPI`, the same as `PageClipboard::IMAGE_DPI`), at most about 4 megapixels
    (`region::MAX_PIXELS`);
  - **High (300 dpi)** and **Very high (600 dpi)**: at least that (more where the screen shows more), at most 36
    megapixels (`snip::HIGH_MAX_PIXELS`: A4 at 600 dpi is 34.8; a picture of 36 MP takes 144 MB while it is made and
    copied, which a desktop takes in its stride).
  A bigger area is drawn with fewer pixels. The note after a snip names the size, "Copied picture (1000×417 pixels,
  300 dpi)", and says when it was made smaller: "… 600 dpi: the area is too large for more". The PNG carries its resolution (dots per meter), so other apps
  paste it at the size it had on the page.
- **The lasso:** the picture is the lasso's bounding box, transparent outside its shape (an antialiased edge).
- **On the clipboard:** the picture (Qt offers it as `image/png` and the platform's other picture formats; `image/png`
  is also set as it is), and `application/x-xournal-qt-snip` (JSON: `title` "name, page N", `link` with the
  document's absolute path as "Copy link" writes it, `page`, `area` in points). No text: a text editor gets nothing to
  paste but the picture.

## Pasting it

- Into a page of the app (Ctrl+V, the context pill's Paste): the picture as an image, as any pasted picture, but at
  the size it had on its page (made smaller to fit the visible part, as before). Selected, to move it right away.
- **The link:** when the snip came from a document with a file, a note at the bottom offers "Add a link to the source
  page (name, page N)?" with **Add link**; it goes away by itself after a few seconds (nothing modal). Add link puts a
  link marker (as "Copy link" pasted, [links.md](links.md)) under the picture (beside it when there is no room
  below): `[🔗 name, page N](../../relative/name.xopp#page=N&…)`, relative to the document it is pasted into, with
  `pdfpage=` for a page that shows a PDF page and the page's first words for one that does not. One undo step.
- **A document without a file** (new, not saved): no link offered, the picture alone.
- **Markdown** (a `.md`, a Markdown text being written on a page): the picture the Markdown way
  ([md-images.md](md-images.md): saved next to the document, `![](../name.assets/image-….png)`), and the offer adds a
  Markdown link `[name, page N](../path#page=N…)` as a paragraph after the picture.
- Pasted into another app: the picture (at its resolution); the fork's entry is ignored there.

## Code

| Where | What |
| --- | --- |
| `qt/src/render/RegionRender.*`, `RegionImage.h` | The picture of an area of a page (+ an outline): Qt-free (cairo), any thread; the document read under its shared lock, the PDF drawn without it (as `PageRaster`). `RegionImage.h` makes a QImage of it. Reused by `qt/stickers` (only the background: `Request::layers = false`). |
| `qt/src/canvas/Snip.*` | Armed or not (process-wide, as the tool in hand), the clipboard entry. |
| `CanvasPage` (press, release), `CanvasInput` | The rectangle or lasso of the select tool, for a snip: no note taken, no selection made; in text mode and reading-only views too. |
| `CanvasView::snip` / `snipped` | Draws the picture off the UI thread (`std::async`; the view waits for it when it goes). |
| `CanvasView::pasteElements`, `insertImage` (`size`), `offerSnipLink` / `addSnipLink`, `MarkdownEditor` (Ctrl+V) | Pasting, the offer, the link. |
| `qt/src/app/AppSnip.cpp` | Arming (`startSnip`, `cancelSnip`), the clipboard, giving the tool back, `addSnipLink`. |
| `Snip.h` (`Resolution`, `minDpi`, `maxPixels`), `AppController::snipResolution` | The resolution (the setting `snipResolution`). |
| `ToolGroups.qml` (`snip`, `snipResolutions`), `AppButtons.qml` (`snipButton`, `imageMenu`), `DocumentNotices.qml` (the snackbar's offer), `WindowShortcuts.qml` (Shift+S / Shift+L, Escape), `ToolCycleButton.qml` (the resolution in its list), `ToolEntryEditor.qml`, `SettingsPage.qml` | The UI. |

Tests: `RegionRender.*` (`-L unit`: the ink and the PDF in the area at their place, only the part on the page, the
background alone, the lasso's transparency, the scale and the size limit), `CopyToolsTest.snipHasAButtonOfItsOwn`
(`-L ui`: the rail's button, its cycle, Shift+S, not in the select list), `SnipTest.*` (`-L ui`:
the resolution's effect on the picture's size and the note, the limit at the screen's; the snip button's list and
the image menu, the clipboard's formats and source, the tool given back, Escape and other tools, a view for reading
only, the pasted size and the link marker, no offer without a file, the Markdown link in a `.md`).

## Not built

- The editor beside the page (the Markdown panel) pastes the picture as before but offers no link.
- A snip across pages: the rectangle or lasso is on the page it started on (as a selection).
- Pasting into the reference while it is written in offers the link as elsewhere; pasting into another window of
  the app works, the offer comes in that window.
