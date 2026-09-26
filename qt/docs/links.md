# Links between documents: design draft

Status: **design agreed 2026-09-24** (the author: "the link plan is sound"); **built in `qt/links`** (see "What is built" at the end for what was built and where it differs). Goal (the author's words): link to
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
  PDF links today; for the mouse see "Links with the mouse" at the end) shows a small popup: **"Open in a new tab" · "Open as reference" · "Open here"** (the last
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

## Decided (2026-09-24)

The author accepted the plan with its proposals:
1. **Links on ink and images get a small visible marker** (a chain icon next to the object); the object itself
   does not become tappable.
2. **Links in other files are rewritten automatically** after a rename or move in the app, with a note ("Updated
   3 links").
3. **A tap asks** (new tab, reference view, here) with "Remember my choice"; the default can be changed in
   Settings → Documents.

## Build plan (`qt/links`, after the running blocks)

1. The link format: a parser and writer, and resolution with the fallbacks (chapter, then page, then fingerprint);
   unit tests.
2. Following links: in Markdown boxes and `.md` files, the popup, back and forward, reference view.
3. Making links: "Copy link" everywhere, pasting into Markdown and markers, drag and drop.
4. The library: outgoing links in the index, backlinks, rewriting on rename and move, the fallback search.
5. The hybrid PDF: `GoToR` link annotations.

## What is built (`qt/links`)

### 1. The link format (`qt/src/session/DocumentLink.*`, tests `DocumentLinkTest`)
- `links::parse` reads a Markdown link target: a path (or a `file://` URL) of a file the app opens as a document
  (`.xopp`, `.xoj`, `.pdf`, `.md`, `.txt`, images, `.tex`, …). Web and mail addresses, other schemes, other files
  (`/home/x.sh`: a tap never opens anything else on the computer), upstream's `#Page:12` and a bare `#anchor` are
  not links to documents (so `example.org/page` stays a web address). A path of `<…>` and percent-escapes (`%20`) are read as Markdown writes
  them. A target without a path but with a place (`#page=5`, `#chapter=…`) points into the same document.
- The fragment: `chapter=`, `heading=`, `page=`, `pdfpage=`, `line=`, and **`text=`**, the page's fingerprint (its
  first five words, normalised, at most 48 characters), written only for a page that shows no PDF page. Unknown keys
  (`zoom=`, …) are left out. A plain fragment is a heading of a `.md` (`note.md#blade-design`), else a chapter.
- `links::parseWiki` reads `[[note#heading]]` (the name is looked up later: it has no extension).
- `links::write` writes the fragment in the order chapter, heading, page, pdfpage, line, text, and escapes what a
  Markdown link cannot hold (space, `%`, `#`, `?`, parentheses, brackets, `<` `>`; in values also `&`, `=`, `+`).
  Letters beyond ASCII stay as they are (`Übung%203.xopp`). `links::markdown` gives `[title](link)`.
- Paths are relative to the folder of the document that holds the link (`links::relativePath`, `resolvePath`).
- Where a link leads (`links::resolve`, on a list of the target's chapters and pages): the chapter by its title,
  then by its normalised title (case folded, only letters and digits); the PDF page (`pdfpage=`); the page number,
  checked against the fingerprint (the nearest page with that text, an earlier one on a tie). What was not found
  gives the note: "Chapter "Correction" not found, opened page 12", "PDF page 9 not found, opened page 1", "Page 99
  not found, opened page 30".
- In a Markdown text (`links::resolveInText`): the heading whose slug (GitHub's: lower case, punctuation dropped,
  spaces to `-`) is the link's (a heading written as text, as Obsidian does, is slugged first), else the line, with
  "Heading "…" not found, opened line 40".

### 2. Following links (`qt/src/app/AppLinks.cpp`, `qt/src/shell/DocumentLinks.*`, tests `DocumentLinksTest` in `-L ui`)
- A tap on a link in a Markdown box or a `.md` (as before: a finger, the mouse or the pen with the hand or a select
  tool; Ctrl + click in a `.md`) that leads to another document shows the popup with the document's name and the
  place ("kalman.xopp, chapter "Prediction step""), **Open in a new tab**, **Open as reference**, **Open here** and
  **Remember my choice**. Remembered, a tap opens the document at once; Settings → Documents → Links → "A link to
  another document opens" (`linkOpening` in the `xournalQt` part of the settings: `ask`, `tab`, `reference`,
  `here`) changes it back. A link into the same document (`#page=5`, a link to its own file) asks **Go there** or
  **In the reference** (the document beside itself, [reference-view.md](reference-view.md)); a remembered
  `reference` opens it there at once, any other remembered choice goes there. A PDF link to a page offers "Go to
  page N" and "In the reference" as well. A web address still shows "Open".
- The file: the path relative to the document holding the link (a new document: relative to the library); a PDF
  with its `.xopp` opens as the `.xopp`, as in the library. A wiki link's name is looked for next to the document
  (with `.md` added), then in the library index by file name (`LibraryIndex::filesNamed`; a `.md` first, then the
  closest folder). A file that is not there: "Document not found" (the search for a moved file is step 4).
- An open document is switched to (its tab), not opened twice. The place is looked up in it
  (`DocumentLinks::placeIn`: the chapters of its contents, its pages; a `.md` by heading and line); a note says what
  was not found.
- "Open here" opens the document in place of the current one, which closes if it has no unsaved changes (with
  changes it stays open behind). Back opens it again, at the page it was left at, in place of the other.
- Back and Forward (Alt+Left/Right, the ← → pill) go across documents: a followed link is remembered with where it
  came from; Back first goes through the places jumped to in the document since the link was followed, then back to
  the document the link was in (its tab, or its file opened again), and Forward returns.
- A link in the reference opens in a new tab (resolved from the reference's own file). When the reference is the
  document itself, a link to a place of it goes there in the reference.
- Markdown boxes keep whether a link is a `[[wiki link]]` (`md::LinkHit::wiki`), so a tap looks the name up.

### 3. Making links (`AppLinks.cpp`, `CanvasView::pasteLinkMarker`, `links::toMime`)
- **Copy link**: the page menu of the sidebar and the page grid ("Copy a link to this page"), ⋮ → "Copy link to
  this page", the contents in the sidebar (press and hold or right-click a chapter → "Copy link to this chapter"),
  a library card's menu ("Copy link"; also on search results), and a page with hits in the extended search (press
  and hold or right-click → "Copy link to this page"). The page of a document that is not open is linked from the
  library index (its PDF page, or its text for the fingerprint: `LibraryIndex::linkPages`).
- The clipboard holds the link three ways: the app's own format `application/x-xournalqt-link` (the title and the
  link with the target's absolute path), Markdown `[kalman, page 4](/abs/Lectures/kalman.xopp#page=4&text=…)` as
  text for other apps, and an HTML link to the `file://` URI for rich text editors. (Not a `text/uri-list`: a file
  manager would take that as a file to paste.) Titles: "kalman, page 4", "kalman, Prediction step", "kalman".
  A document without a file yet copies "#Page:12", upstream's link within it, as before.
- **Pasting** into Markdown being written on the page, into a `.md`, or beside the page (the panel) inserts
  `[title](link)` with the path relative to that document (a new document without a file: the absolute path).
- **Pasting on a page** (Ctrl+V, the paste of the context pill) makes a **link marker**: a small Markdown text box in
  the page's Markdown layer (made if needed) holding `[🔗 title](link)`, in the link color, as wide as its text.
  Xournal++ shows it as that text. It goes where it was pasted, or in the middle of the visible page; with elements
  selected, at their top right ("this sketch links to …"). One undo step (plus one for a Markdown layer made for
  it). A tap on it follows the link like any link in a Markdown box.
- **Not built: dragging** a card, a page or a chapter onto the page. The library is a screen of its own (never beside
  a page), and dragging a page in the sidebar or the grid moves it; a drop target on the canvas for these would need
  a new drag source in each list. Copy link and paste do the same in two steps.

### 4. The library: links in the index, backlinks, rewriting, the search for a moved file
(`qt/src/shell/LinkRewrite.*`, `DocumentLinks::backlinks` / `findMoved`, `AppLinks.cpp`; tests `LinkRewrite.*` in
`-L shell`, `DocumentLinksTest` in `-L ui`)
- **Outgoing links in the index**: besides a Markdown file's links (as before), a `.xopp`'s entry in `notes.pack`
  now has the links and wiki links of its Markdown boxes and link markers (`links`, `wikiLinks`). An entry of notes
  written before has no `links` key: its `.xopp` is read once more (only the `.xopp`: its PDF text is kept), no
  format change. Entries converted from the layout before the packs learn their links when the `.xopp` is saved.
  `LibraryIndex::linkSources` lists them.
- **Backlinks**: ⋮ → **Linked from…** lists the documents of the library whose links lead to the current one (a
  link to any of its files: the PDF of a `.xopp` counts; a wiki link by its name); a tap opens one.
- **Rewritten after a rename or move in the app** (the library's Rename, Move to…, dragging onto a folder; also whole
  folders; a rename from a tab, ⋮ → Rename… or the overview of open documents, which is the library's rename):
  `LinkRewrite::plan` finds the links that point elsewhere now - links to what moved, and the relative links of a moved
  document itself - and writes each anew relative to where it is (the fragment stays; a wiki link to a renamed document
  gets the new name). Only link targets change: `](…)`, `](<…>)`, `[id]: …`, `[[…]]`; the rest of the text stays byte
  for byte.
  - Open documents change through themselves, with undo (a `.xopp`'s texts as text edits, a `.md` as one edit of
    its text), and are saved when they had no unsaved changes (so the file has the new link too); with unsaved
    changes they keep the change until they are saved.
  - The others in the background: a `.md` through its text file (`TextFile`: byte for byte where nothing changed,
    written atomically), a `.xopp` loaded and written again. Then the note **"Updated N links"**, and the library
    reads them again. A hybrid PDF and an old `.xoj` that are not open are left as they are (said in the code; not
    in the note).
- **A link whose file is gone** (moved outside the app): the library index is asked for a document of that file
  name (the closest to the linking document; a PDF with its `.xopp` by its name without the extension), then for a
  page with the link's fingerprint. Found, it opens (at the place the link says) and the window asks **"The linked
  document was moved … Update the link to point there?"**; Yes rewrites the link in the document it was followed
  from (through it, with undo). Not found: **"Document not found … Locate it?"**, a file dialog, and the link is
  written anew to the chosen file and followed.
  - **Deviation:** the PDF's `/ID` is not used. A link does not carry it (the format has no key for it), so there
    is nothing to compare; the name and the page's text cover the cases seen so far. A `pdfid=` key could be added
    to the fragment later.

### 5. The hybrid PDF (`HybridPdf.cpp`: `linkFor`, `annotateLinks`; `md::linkBoxes`; test `HybridPdfTest.linksOf…`)
- Saving a PDF with notes writes each link of the Markdown boxes and link markers as a `/Link` annotation over the
  link text (a box per line it is on), so other viewers follow it:
  - a link to a PDF: `/GoToR` with `/F` the PDF relative to the hybrid PDF and `/D [page /Fit]`, `/NewWindow true`
    (the `pdfpage=` of a plain PDF, else `page=`; a hybrid PDF's pages are its document's pages);
  - a link to a `.xopp` with its PDF next to it (`lecture.xopp` + `lecture.pdf`): `/GoToR` to that PDF, at the
    link's PDF page (a notes page inserted there has no PDF page: then the page number, which may be off);
  - a web or mail address: `/URI`;
  - a `.md`, a `.xopp` without a PDF, a place in this document: no annotation (other viewers could not open them).
- Paths are resolved from the hybrid PDF's folder. The annotations are ours (`/NM (xopp:p1-link1)` and the private
  key): removed and written again with the rest on every save, in the clean copy never shown, and never reported as
  changed by another app (they are made from the text).
- Not handled: a base PDF page that is rotated or has a crop box moved from the origin gets the link boxes offset
  by the crop box only (the ink uses the full placement matrix).

## Links with the mouse, and their address on hover (`qt/link-hover`)

The author (2026-09-26): links should be clickable with the mouse on the desktop, not only with a finger, and hovering
a link with the mouse or the pen should show where it leads, at the bottom like a browser, without getting in the
way when the pointer only passes over it.

### A click follows a link (`CanvasInput`, `CanvasView::hoverLinkAt` / `followLinkAt`; tests `LinkMouseTest` in `-L canvas`)
- A click is a press and release of the left button that moved no further than the platform's drag distance
  (`QStyleHints::startDragDistance`, about 10 pixels), however long it took. It follows the link as a finger's tap
  does: `linkTapped`, so the same sheet (new tab, reference, here), the same remembered choice, the same "Go to
  page N" for a PDF link. In the reference it is the reference's sheet.
- The links: PDF link annotations, link markers, web addresses and `#Page:N` in text boxes, and the links of
  Markdown boxes and pages.
- **Which tool follows on a plain click:**
  - **the hand, the select tools and the PDF text tools:** yes. The object select tool first selects what it
    clicks (a link marker is a text box it can take); with nothing selected, the release follows the link, as
    before;
  - **the pen, the highlighter (also their shapes), the eraser and the laser pointers:** yes, and nothing is drawn:
    on a link the press waits whether it becomes a drag. Moving beyond the drag distance starts the stroke where
    the mouse was pressed, so a stroke that begins on a link is drawn in full. A single dot on a link is not
    possible with the mouse (the click follows the link); a spline's clicks stay its points;
  - **the text tool, the image tool, vertical space and the others that place something:** no, the click does what
    the tool does; Ctrl + click follows;
  - **text being written** (the Markdown box or page with the cursor, a text box being edited, a `.md` or `.txt`
    file): a plain click puts the cursor there, as in editors; **Ctrl + click follows**, as before;
  - **Ctrl + click follows with every tool.**
  - A document shown only for reading (a `.md` shown, the reference): every tool follows.
  - With a selection out (elements, a sticky note, PDF text), the click ends it and follows nothing, as a tap does.
- The pen keeps writing with the drawing tools (a tap of the pen on a link is a dot, as before): only the mouse
  waits for the click.
- The links under the mouse come from what each page keeps (`CanvasPage::linkSpots`): looked for once (the PDF's
  link annotations through poppler, the texts' web addresses, the Markdown layouts' link boxes) and forgotten on
  any change of the page (a stroke, a text, undo, a layer shown or hidden). A move over the page is a walk over a
  few rectangles; `CanvasView::linkLookups` counts the searches (a test moves 400 times and sees one).

### The pointing hand and the status line (`DocumentCanvasItem::linkHovers`, `LinkStatusLine.qml`, `AppController::linkPreview`; tests `CanvasItemInputTest` in `-L quick`, `DocumentLinksTest` in `-L ui`)
- Over a link that a click follows (the rules above, with the Ctrl key as it is now) the mouse's cursor is a
  pointing hand, else the tool's cross. Over text being written it is the hand only while Ctrl is held; pressing or
  letting go of Ctrl updates it without moving. The hovering pen keeps its cursor (it writes on links).
- **The status line**, as in a browser: a small line at the bottom left of the canvas the pointer is over (the
  notes, or the reference beside them: each has its own), showing where the link leads:
  - a web or mail address: in full;
  - a page of this document (a PDF link inside the PDF, `#Page:N`, a link to a place in this document): "Page 12",
    with its chapter when the document has one there ("Page 12 · Prediction step");
  - a PDF link to a page the document does not have: "PDF page 9 (not in this document)";
  - a link to another document: its file name and the place ("kalman.xopp, chapter “Prediction step”",
    "turbines.md, heading “blade-design”", "lecture.pdf, page 3"), a wiki link by the file it finds;
  - a document that is not there: "lost.xopp (not found)" (the search for a moved file is only done when the
    link is followed);
  - any other target of a Markdown link: as written.
- It comes once the pointer rested on a link for 300 ms (passing over links shows nothing), fades in quickly and
  out quickly (150 ms) when the pointer leaves; moving from one link straight to another changes it at once.
- It never takes a press or the focus (`enabled: false`, looked through by the canvas's hit test), and it never
  covers the pointer: when the pointer is where it would be, it moves to the bottom right.
- Its colours follow the Material theme (a light grey in the light theme, a dark grey in the dark one); the text
  is elided in the middle beyond 60 % of the canvas width.
- The mouse (and a touchpad) and the pen's hover (tablet moves without a button while it is near) show it; a
  control, a menu or a popup over the canvas hides it (the canvas item's hit test is asked only when the link under
  the pointer changes). Touch has no hover: on a phone or tablet without a pen it never shows. It is not shown on a
  long press either (that opens the context menu).
- The text is made when the link changes (`app.linkPreview`: the file's existence, the chapters), not per move.

