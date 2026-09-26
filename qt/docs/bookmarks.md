# Favourites and bookmarks (`qt/bookmarks`)

Decided with the author (2026-09-26): **favourites** (stars) for documents and **bookmarks** for pages. They are
stored differently on purpose:

| | Favourite (★) | Bookmark (ribbon) |
| --- | --- | --- |
| belongs to | the user's relation to a file | the document's content |
| stored | beside the document, never in it | in the document |
| travels with the file | no (renames and moves in the app take it along) | yes (sync, e-mail, other devices) |

## Favourites

### Storage
A star is not content: writing it into the file would change read-only PDFs, make sync apps upload the file again,
and put it at the top of "recently changed". So it is kept like the title page and the reading position
(`qt/src/shell/DocumentPlaces.*`, key `"star"`):

- documents of the library: in the library's folder in the config (`~/.config/xournal-qt/libraries/<key>/pages.json`),
  by their path in the library;
- other documents: in the user's cache (`~/.cache/xournal-qt/documents/pages.json`), by their whole path.

The key is the document's PDF when it has one (a PDF keeps its star when its `.xopp` comes), else its main file
(`DocumentPlaces::keyOf`). Renaming and moving in the app (library, tab rename, Recent) go through
`DocumentPlaces::moved`, so the star follows. **Moving or renaming the file with another program loses the star**
(the same as the reading position). A cleaned cache loses the stars of documents outside libraries.

### Where it shows
- Library cards: a filled star at the top left of a starred card (tap: remove it); under the mouse every card shows
  an empty star (click: add). The card's menu has "Add to favourites" / "Remove from favourites" (touch).
- Recent cards: the same star.
- The open document: ⋮ → "Add to favourites" / "Remove from favourites".
- The overview of open documents: the star beside the close button (starred ones always, others under the mouse).
- Library home: the **Favourites** chip next to the "Show" filter (not in a menu; its word shows in windows 1500 px
  wide or more, else the star alone with a tip, so the header still fits 1280 px). With it on, the grid lists the
  starred documents of the **whole library** without folders (like the flat list), combined with the "Show" filter and
  the search. The Bookmarks view follows it too. The chip is not remembered across starts.

## Bookmarks

### The model
A bookmark is an optional label on a page: `XojPage::getBookmark()` → `std::optional<std::string>` (an upstream seam,
listed in [ADR 0002](adr/0002-upstream-seams.md)). An **empty label is the automatic one**: the page is called
"Page N" by its place *now*, so it stays right when pages come, go or move. Because the label lives on the page
object, it follows its page through insertions, deletions, moves and their undo without any bookkeeping in the Qt
layer (the task allowed a PageRef-keyed store in the Qt layer; a field is simpler and survives every page operation,
including the background save's copy of the pages). A duplicated or pasted page starts without the bookmark.

- `DocumentSession::setBookmark(page, label | nullopt)`: set, rename, remove; **one undo step** each
  (`PageBookmarks::BookmarkUndoAction`, "Add / Rename / Remove bookmark"); `bookmarksChanged()`.
- A new bookmark's label: the page's first heading (`DocumentChapters`: `# `-texts, text-mode headings, Markdown box
  headings), else the entry of the PDF's own table of contents that goes to its PDF page, else the automatic label.
  Typing "Page N" or nothing in the rename dialog makes it automatic again.

### .xopp
The page attribute `xqt-bookmark="label"` on `<page>`, written only on bookmarked pages (`xqt-bookmark=""`: the
automatic label). Upstream's `XmlParser` looks attributes up by name (`XmlParserHelper::AttributeMap::operator[]`)
and never complains about unknown ones (unknown *tags* are what triggers `logError` and "Save as", so none are added).
`BookmarksTest.upstreamXournalppOpensItWithoutAMessage` runs the upstream binary on such a file.
**Saving the file in Xournal++ drops the bookmarks.**

### PDF with notes (hybrid PDFs, archive PDFs)
`PdfBookmarks` writes the bookmarks as the children of a **last top-level outline item "Bookmarks"** (marked
`/XournalQt true`), so every PDF viewer lists them next to the document's own table of contents, which is left as
it is. Each child is `/Title (label)` with `/Dest [page /XYZ null null null]` (the viewer keeps its zoom); the
automatic label is written as "Page N". The item is open (`/Count` = the number of bookmarks; the outline
dictionary's `/Count` follows).

- **Full write** (`HybridPdf::write` in full, `writeArchive`, and the base pages of "For Xournal++"): our item is
  removed from the copied outline and written again from the document.
- **Incremental save** (Ctrl+S, qt/docs/hybrid-pdf.md): nothing of the outline is touched when the bookmarks are as
  the file has them (same titles, same page objects). Otherwise our item is written again in its place (new
  children; the old ones stay unreachable until the next full write), and when the item comes or goes, the outline
  dictionary and the neighbouring item are touched. Tested with `qpdf --check` after every revision.
- **Reading**: the embedded `document.xopp` carries the bookmarks too, and that is what the app reads. The outline
  item is for other viewers. A PDF *without* our data whose outline has such an item (a copy another app wrote
  again, a plain export) gets its bookmarks from it when opened (`PageBookmarks::adoptOutline`: each entry's page →
  the first page showing that PDF page; "Page N" to page N → automatic).
- **Recognising our item**: `/XournalQt` (qpdf), or — for poppler, which does not show private keys — a top-level
  item titled "Bookmarks" whose children all go to a page and have no children. The table of contents (contents
  sidebar, contents overview, chapter links, the annotations export) leaves it out. A book whose own table of
  contents has a flat top-level chapter called exactly "Bookmarks" would be taken for ours (not seen in practice).
- Bookmarks changed in another PDF app are not read back while the file carries our data (the embedded document
  wins, and the next save writes the item again).

### Markdown (`qt/md-bookmarks`)
Decided with the author (2026-09-26). In a Markdown text a bookmark is an **HTML comment on a line of its own, right
before the block it marks** (a paragraph, a heading, a list, a table, a code block):

```markdown
<!-- xqt:bookmark Proof of theorem 3.2 -->
## Proof
```

`<!-- xqt:bookmark -->` (no label) is the **automatic** one: named after the heading it marks, else the first
heading of its page, else "Page N".

**Why a comment.** A `.md` is a plain text other apps edit too: no attribute or side file would survive them. It
follows the convention of the fork's own markers (`<!-- xqt:cont … -->` of the pages, `<!-- xqt:plain -->`), GitHub,
Obsidian, Typora, pandoc and the other renderers hide it, and it **moves with the text**: text added above it, a
reflow, an edit in another editor take it along with its block. A position (line number, byte offset) kept elsewhere
would not.

- **Read with the parser**, never with a text search (`qt/src/markdown/MdBookmarks.*`): a top-level HTML block that is
  exactly such a comment (spaces around it are fine; text after it, several lines, or `-->` in the label are not).
  So an example in a code block, in `` `code` ``, in a quote or in a list is not a bookmark. A plain text (`.txt`)
  has none.
- **Its page** is the page the block after it starts on: the pagination (`MdPaginate`) keeps a comment with the
  block after it, as it keeps a heading (and a page with nothing but comments before a block that does not fit is
  not made: the block stays and goes below the margin, as before). A bookmark at the very end of the text is on the
  last page. Several bookmarks on one page: the page's bookmark is the first one (its label); removing the page's
  bookmark removes all of them.
- **Labels** are written on one line (line breaks and tabs become spaces), without `--` (it would end or invalidate
  the comment: it becomes "–"), trimmed. Reading keeps the label as written (trimmed).
- **Pages ← text:** the bookmarks of the pages of the text are read from their slices into `XojPage::bookmark`
  (`TextDocument::syncBookmarks`: the page's first bookmark comment, its label or the automatic one resolved) when a
  document is made or loaded, after every change of the text (`MarkdownSession`), and after undo / redo (queued: the
  undo handler may call back under the document's lock). So the pages sidebar (ribbons), the contents sidebar, the
  library and the PDF's outline read them as they read any page's bookmark, with no code of their own.
- **Which pages:** the pages of the text that starts on page 1 (`TextDocument::hasTextBookmarks`): a `.md` (and the
  `.md` with its `.assets` folder), a PDF text document ([md-pdf.md](md-pdf.md)), and so also a `.xopp` whose page 1
  starts the page's Markdown text (a text document by the same rule). Other pages keep the page attribute: a page of
  notes after the text, Markdown text boxes, a `.xopp` whose text starts on a later page. A page attribute written
  by the `qt/bookmarks` build on a page of such a text is replaced by the comments (there were none then): it is
  lost when the document is opened (not migrated; that build was merged the same day).
- **Bookmark this page** (page menu, ⋮) on such a page is an **edit of the text** (`MarkdownBookmarks::edit`,
  `qt/src/canvas`): `<!-- xqt:bookmark -->` is inserted before the **first block that starts on the page** (a page
  that begins inside a block: the first block after it; comments are skipped). **Decision:** a page that is all
  inside one block (a long code block, list or table) has no place of its own: the comment goes before the block
  that goes on over it, so the bookmark is on the page that block starts on, and the note says so ("Bookmark added
  on page 3, where the text of this page begins"); if that block has a bookmark already, nothing is added. Removing
  deletes the page's comment lines; renaming rewrites the first one ("Page N", or the automatic label itself, makes
  it automatic again).
  - In a `.md` with the cursor in the text it is a step of the text being written (`MarkdownEditor::applyEdit`, its
    undo, Ctrl+Z steps back through it); the cursor stays where it was in the text. Without a cursor, and in a PDF
    text document (whose undo takes back a whole edit of the text), it is an edit of its own: one undo step
    (`MarkdownFile::setText`). Saving writes it as any typed text (byte for byte elsewhere).
- **In the text being written** the comment's line is not drawn and takes no room (as every comment). The cursor is
  never on it: moved onto it, it goes past it the way it went (Left from the start of the marked block: to the end
  of the line before; Right: back). Backspace at the start of the marked block, or Delete at the end of the line
  before it, removes the whole line (one step, undoable); a selection across it takes it along. The Markdown source
  beside the page shows it as it is, and a comment typed there (or on the page, or in another editor) is a bookmark
  as soon as its line is complete.
- **PDF text documents:** the comments are in the flow, so in the embedded `document.xopp` and in the plain
  `name.md` other apps get; each save also writes them into the PDF's outline item "Bookmarks" (the pages' bookmarks,
  as above: nothing new in `HybridPdf`). Tested with `qpdf --check` after full and incremental saves.
- **Library:** the index reads a `.md` with a bookmark comment on the pages it opens on (`MarkdownFile::document`
  with its pictures, only for files that contain `xqt:bookmark`), into its entry's bookmarks (the "notes" pack as for
  other documents; a Markdown entry has no pages there, so their aspect is A4). A comment's passage is indexed as the
  label, in front of the passage of the block it marks (so the search finds the label, the card shows that block).
  The Bookmarks view draws a `.md`'s page with `HitPageProvider` like the others (the file laid out with its
  pictures).
- **Not yet:** on a continuous page (`textContinuous`) every bookmark is on the one page (its ribbon shows the first),
  while the library still lists them on the pages the file has on pages; going to the comment's place on the long
  page is not done. Markdown text boxes and flows that start after page 1 of a `.xopp` keep the page attribute.

### Library cache
`LibraryIndex` reads a document's bookmarks with its pages (`fillPages`, also for a document saved in the app) into
its entry, and keeps them in the folder's **"notes" pack** (`"bookmarks": {page: label}`, only when there are any).
Entries of older versions have no key: their files had no bookmarks (a file changed since is read again), so no
document is read again for this. `LibraryIndex::bookmarks()` lists them all; `bookmarkChanges()` changes when they
do. The library search also matches a page's bookmark label (plain and fuzzy search; not the automatic "Page N").

### Where it shows
- Page menu (the sidebar's ⋮ and press and hold, the page grid): a bookmark icon beside "Background… | Size…" (the
  menu stays as short as it was): on a page without one it adds it; on a bookmarked page it opens the bookmark's
  dialog (rename, or "Remove bookmark").
- ⋮ menu of the document: "Bookmark this page" / "Remove the bookmark of this page" for the current page.
- A ribbon on the previews of bookmarked pages (sidebar, page grid), and the label after the page number.
- Contents sidebar: a **Bookmarks** section at the top (tap: go there; press and hold / right click: rename, remove,
  copy link). The Contents tab shows when the document has a table of contents *or* bookmarks.
- Library home: the **Bookmarks** tab next to Library and Recent (its word shows while it is open or in a wide
  window, else its icon with a tip) (`BookmarksView.qml`, `LibraryBookmarksModel`): the
  bookmarked pages grouped by document (name, folder, star), each a picture of the page (drawn by
  `HitPageProvider`, as the pages of the extended search: loaded documents and drawn pages are kept) with its label;
  a tap opens the document at that page. It follows the search text (labels and document names), the "Show" filter
  and the Favourites chip. It is made only while shown, from the index.
- The snackbar after adding or removing one offers Undo.

### Not in this block
- Markdown documents: done in `qt/md-bookmarks` (above).
- Plain text files (`.txt`, other text files edited as text): no bookmarks.
- Upstream's plain PDF export (`XojCairoPdfExport`) writes the PDF's outline as it was read, with our item as it was
  when the document was opened; it does not write the current bookmarks.
