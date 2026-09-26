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
- **Markdown documents** (`.md`): no bookmarks; their headings are the chapters already.
- Text files shown or edited as text: no bookmarks.
- Upstream's plain PDF export (`XojCairoPdfExport`) writes the PDF's outline as it was read, with our item as it was
  when the document was opened; it does not write the current bookmarks.
