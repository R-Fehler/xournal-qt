# Links between documents: design draft

Status: **draft, 2026-09-24**; the points under "To decide" need the author. Goal (the author's words): link to
another document, optionally to a page or a chapter; if it is a chapter, keep its page as a fallback for when the
name changes. When the link is followed, choose between a new tab and reference view.

## What a link is

A link is written as one URI that other programs also understand as far as they can:

```
../Lectures/Kalman filter.xopp#page=12
../Lectures/Kalman filter.pdf#page=12
../Lectures/Kalman filter.xopp#chapter=Prediction%20step&page=12
../Notes/turbines.md#heading=blade-design&line=40
```

- **The path is relative to the document that holds the link**, like Markdown and HTML links. A library, or any
  folder, moved or synced as a whole keeps working, including on another machine or under another user name.
- **`#page=N`** (1-based) is the PDF "open parameters" form. Acrobat, pdf.js and most viewers follow it for PDFs.
- **`#chapter=…&page=N`**: the chapter's title, with its page as the fallback, as the author asked.
  - Chapters are the document's headings: the PDF outline, Markdown headings, and text-mode headings (the "Contents"
    sidebar already lists them).
  - When following a link, the chapter is looked up by title, then by title ignoring case and punctuation. If it is
    not found, the saved page is used, with a note: "Chapter 'Prediction step' not found, opened page 12".
- **`#heading=…&line=N`** for `.md` files: the heading's slug, like Obsidian and GitHub anchors, with its line as
  the fallback. A plain `note.md#heading` also works.
- Obsidian's `[[note#heading]]` is read as the same thing inside vaults.

## A page is more than its number

Pages move: inserted, deleted, reordered. So a page link also stores what the page shows, and uses whichever
survived:

1. For a page that shows a PDF page: the **PDF page number** (`pdfpage=7`). It does not change when notes pages are
   inserted around it, which is exactly the lecture case.
2. Otherwise the page number, checked against a short **text fingerprint** of the page (the first words of its
   text, if it has any): if page 12 no longer has that text but page 13 does, go to 13.

Written as `#page=12&pdfpage=7`. Viewers that know only `page=` still get close.

## Where links live

- **In Markdown boxes and `.md` files:** ordinary Markdown links, `[Kalman, prediction](../Lectures/Kalman.xopp#chapter=…&page=12)`.
  This is stored as plain text, so the file stays upstream-compatible (Xournal++ shows the link text as source).
- **In the hybrid PDF:** also as a real PDF `/Link` annotation with a `GoToR` action (a file plus a page), so
  Acrobat and other viewers follow it too.
- **On ink or images** ("this sketch links to …"): a small link marker element placed on the page. It is saved as
  a text element in the Markdown layer holding a Markdown link, so it survives in Xournal++ as readable text.
  Tapping the marker follows the link.

## Making a link

- **"Copy link"** on a page (sidebar, page grid), a chapter (Contents), a library card, a search hit, or the
  current page (⋮ menu). The clipboard then holds the link, as a URI and as Markdown.
- **Pasting** it into a Markdown box inserts `[title](link)`. Pasting it on the page with nothing selected creates
  a link marker. Pasting it with a selection ("link this") attaches it to the selection as a marker next to it.
- **Dragging** a library card, a page from the sidebar or grid, or a chapter onto the page does the same.

## Following a link

- A tap on a link (finger, mouse, or the pen with the hand or select tool; the pen keeps writing otherwise, as with
  PDF links today) shows a small popup: **"Open in a new tab" · "Open as reference" · "Open here"** (the last
  replaces the current view, with Back to return). "Remember my choice" makes it the default, which can be changed
  in Settings → Documents.
- Back and forward (Alt+Left/Right, the ← → pill) work across documents.
- An open document is switched to, not opened twice.

## Keeping links working

- **Moves and renames inside the app:** the library index already records each document's text, so it also records
  its **outgoing links**. When a document is renamed or moved in the app, the documents that link to it are known
  (backlinks), and their links are rewritten, in the background with a note ("Updated 3 links"). For `.xopp` files
  that are open, the change goes through the document with undo.
- **Moves outside the app:** following a link whose path is gone looks for the target in the library index by file
  name, then by the PDF's `/ID` (for PDFs and hybrid PDFs), then by the page fingerprint. If found, the link opens
  and offers "Update the link". If not, it asks to locate the file.
- **Backlinks** come for free from the same index: a small "Linked from" list for the current document (sidebar or ⋮
  menu), useful for notes that reference each other.

## To decide (the author)

1. **Link markers on ink and images:** is a small visible marker (a chain icon next to the object) right, or should
   the object itself become tappable, which is harder to see and harder to tell from writing?
2. **Rewriting links in other files** after a rename or move in the app: automatic, with a note ("Updated 3
   links"), or ask first?
3. **The default action on a tap:** always ask (with "remember"), or reference view by default, since the main use
   is reading a source next to the notes?

## Build plan (`qt/links`, after the running blocks)

1. The link format: a parser and writer, and resolution with the fallbacks (chapter, then page, then fingerprint);
   unit tests.
2. Following links: in Markdown boxes and `.md` files, the popup, back and forward, reference view.
3. Making links: "Copy link" everywhere, pasting into Markdown and markers, drag and drop.
4. The library: outgoing links in the index, backlinks, rewriting on rename and move, the fallback search.
5. The hybrid PDF: `GoToR` link annotations.
