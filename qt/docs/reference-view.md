# The reference view

A second document beside the one being written in, in the same tab (`qt/reference-view`, `qt/reference-popout`,
`qt/self-reference`). Code: `ReferenceMode` (the controller behind `app.reference`), `TabManager` (which tab shows
what beside it), `ReferenceSplit.qml` (the split, the divider, the pill). Tests: `ReferenceModeTest` (`-L shell`),
`ReferenceWindowTest` (`-L ui`), `SecondViewTest` (`-L canvas`).

## Another document beside the notes

- Opened with "Open as reference" (the tab strip's menu of another tab, the book on a card of the tab overview,
  a library card, a link's popup). The reference is another open tab; each tab has at most one.
- For reading by default: tools scroll there, selections can be copied but not moved. The pen button of its pill
  lets the tool in hand write there too (per tab; off for every new reference).
- The pill: page number (go to a page), the page grid, edit, copy, fit width, swap sides, swap roles (the reference
  becomes the notes), "Show as a tab", close. Keys act on the side tapped last.

## The same document beside itself (`qt/self-reference`)

For cross-referencing within one large document: the tab's own document shows in a second view beside it.

- **Opened with** "Show this document beside" in the menu of the current tab (tab strip) and on the current
  document's card in the tab overview (both start at the page in view), the book icon of the page menu (sidebar and
  page grid; it starts at that page), "In the reference" in the popup of a link to a page of the document (a PDF
  link, `#page=`, a chapter), and "Open as reference" on the document's own file.
- **One document, two views.** One `DocumentSession` (one undo history, one autosave, one search, the same page
  revisions and previews), two `CanvasView`s. Each view has its own scroll position, zoom, current page, selection,
  PDF text selection, geometry tools and Back/Forward. What is written on one side shows on the other.
- **Which view is the tab's.** The first view of a session is its primary view (`DocumentSession::isPrimaryView`):
  the session's current page is its page, so the page sidebar, the page number, the models, search hits and undo
  follow it and scroll only it. The second view keeps a page of its own and is never moved by the session; while it
  acts (a press, a release, an action of its pill), reused upstream code sees it and its page
  (`DocumentSession::ViewScope`), so a stroke, a paste or "select all" lands on its page. When pages are inserted
  or deleted elsewhere it stays on the page its reader was on.
- **Read-only by default**, with the pill's pen button, as for another document. Undo in either side undoes the
  last change of the document.
- **Memory.** Both views register with `CanvasMemory` like any view: the limit is shared, not doubled; the view used
  last gets the larger part, the other one keeps its visible pages. Previews and thumbnails are per document, so
  they are shared too.
- **Swap roles** exchanges the places of the two views (each keeps its zoom; Back returns on each side); the tab
  stays the same.
- **"Show as a tab"**: the document has one tab, so the tab goes to the place of the reference (Back returns) and
  the split closes.
- **Closing** the reference (its pill, the tab menu) destroys the second view; closing the tab or moving it to
  another window closes the split with it. Another reference replaces it, and the other way round.
- The reference's page grid shows the same pages; a tap there moves the reference, not the notes.
- Links tapped in the second view that lead into the document go there in the reference.
