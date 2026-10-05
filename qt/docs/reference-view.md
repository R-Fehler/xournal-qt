# The reference view

A second document beside the one being written in, in the same tab (`qt/reference-view`, `qt/reference-popout`,
`qt/self-reference`, `qt/version-compare`). Code: `ReferenceMode` (the controller behind `app.reference`), `TabManager`
(which tab shows what beside it), `ReferenceSplit.qml` (the split, the divider, the pill), `ScrollLock` (both sides
scrolled together), `VersionCompare` and `VersionDiff` (comparing versions). Tests: `ReferenceModeTest` (`-L shell`),
`ReferenceWindowTest` and `VersionHistoryTest` (`-L ui`), `SecondViewTest` and `ScrollLockTest` (`-L canvas`),
`VersionDiffTest` (`-L session`).

## Another document beside the notes

- Opened with "Open as reference" (the tab strip's menu of another tab, the book on a card of the tab overview,
  a library card, a link's popup). The reference is another open tab; each tab has at most one.
- For reading by default: tools scroll there, selections can be copied but not moved. The pen button of its pill
  lets the tool in hand write there too (per tab; off for every new reference).
- The pill: page number (go to a page), the page grid, scroll both sides together (see below), edit, copy, fit width,
  swap sides, swap roles (the reference becomes the notes), "Show as a tab", close. Keys act on the side tapped last. In a narrow half (under 480 px: a
  phone, a small window) the pill is the page number and a ⋮ that holds the rest (`referenceMenu`; a sheet on a
  phone), so it stays small.
- **Side by side or top and bottom** (qt/adaptive-panels): side by side where the canvas area is landscape, top and
  bottom where it is portrait (h > w: a tablet or a phone held upright), with 16 px of margin around square so a
  window dragged across does not flicker. "Swap sides" puts the reference at the bottom (top) then. The divider is
  dragged the same way across, and keeps its ratio (the notes' share) when the orientation flips. The notes' view
  pill and the reference's pill each stay inside their half, so they never meet.

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
- **Selecting works as on the notes** (qt/touch-multiselect): the selection's pill for elements and for notes
  selected together (with the count and "Select more"), the note's pill at a selected note (`NotePill` with
  `target: app.reference`; colours, cover, text, image, cut and delete while the view is written in, copy and
  deselect for reading only), Ctrl + click and "Select more" to add and take away (qt/docs/sticky-notes.md, "Select
  more"). Each view has its own selection and its own select more.
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

## Scrolling both sides together, and comparing (`qt/version-compare`)

The author (2026-10-05): "Implement comparing two versions using the reference view but add a locked scroll toggle so
both canvases are scrolled at the same time. Make this generic so we can use that toggle in the reference view to
compare PDFs in general."

### Locked scrolling

- **The toggle** is the pill's "Scroll both sides together" button (`referenceLockButton`, two pages with arrows; in a
  narrow half the item `referenceLockItem` of its ⋮). `app.reference.scrollLocked`. No key by default: the action
  "Scroll the document and the reference together" (`lockScroll`) can be given one in the shortcuts (Ctrl+Alt+L, the
  natural one, locks the screen on KDE and other desktops).
- **For any two views**: two documents, a document beside itself, a version beside now. While on, scrolling, a page
  jump (Go to page, the sidebar, the grid, a link, Back) and zooming on either side move the other (`ScrollLock`, in
  `canvas/`, on the two `ViewController`s).
- **Where:** by page, with the page offset the two have when it is switched on (page 3 here beside page 1 there stays
  so), and by the place within the page relative to its size, so pages of other sizes (slides beside A4) still line
  up. The point kept together is near the top of each view (sideways: near its left edge), so two documents shown from
  their beginnings are on their first pages whatever the size of their pages. Switching it on brings the reference to
  the notes' place within the page (the page offset stays); the zooms stay as they are.
- **Zoom:** relative to the width that fits each half (`fitWidthZoom` of the page in view): the two keep the ratio
  they had when it was switched on. Two halves that both fit the width keep doing so, a pinch on one side zooms the
  other as much, Fit width on one fits both. (Keeping the zooms independent was the other choice; with pages of other
  sizes it made the lines of the two drift apart at once.)
- **No loops, no extra work:** while one view moves the other, the other's signals are not followed, and a view
  already where it should be is not moved; the other side renders what scrolling it needs, nothing more. A view whose
  size changed (the divider, the window, the halves flipping between side by side and top and bottom) follows the
  other one and never moves it. A reference shown again before it has a size opens where it was sent; the pair is
  taken from there.
- **Remembered per pair** of documents (either order, a document with itself) for the session: another tab, closing
  and showing the reference again, swapping roles keep it; off for every new pair. The offset is taken again whenever
  the pair is shown again (they were kept together meanwhile, so it is the same unless one side was scrolled alone in
  its own tab).
- Works in both orientations of the split and on the phone (the ⋮ of the narrow pill).

### Comparing a version (with now, or two versions)

- In the History panel, a row's menu: **"Compare with now"** shows that version beside the document (read-only,
  `VersionCache`) and locks the two; **"Compare with another version…"** then a tap on another row shows the newer of
  the two read-only in a tab of its own (the document's tab stays) with the older beside it, locked.
- **What changed** (`VersionDiff`, in `session/`): each page is signed from what it holds, without drawing it: its
  size, its background (kind, PDF page, colour without its alpha, image file) and every element of every layer as the
  `.xopp` holds it (upstream's writer: a stroke drawn now and the same stroke read back from a saved version give the
  same text, where their numbers in memory differ in the last bits). The two lists are aligned (the equal pages at both ends, then the longest common run), so an inserted or
  removed page marks only itself, and a page that only moved is no change; between two equal pages the changed pages
  of each side are paired in order, the rest are added or removed.
- Signed from the loaded documents rather than from the per-layer sigs in the marker (`/Layers`): "now" has unsaved
  changes the marker does not know, the marker's sigs include the app's version (a version saved by an older build
  would differ everywhere), and the same code compares any two documents. The pages are signed on the GUI thread a
  few milliseconds at a time and kept by page revision (`VersionCompare`, its owner; only the pages of the two
  documents), so writing on "now" during a comparison signs only the page written on again (a moment after writing
  pauses).
- **Shown:** changed pages have a violet bar beside their thumbnail in the page sidebar, the page grid and the
  reference's grid (`PagesModel::DiffersRole`, from `CompareMarks`), on both sides. The comparison's bar at the top
  of the reference (`compareBar`): what is compared ("4 Oct 14:30 → Now"), "2 pages changed, 1 added", previous and
  next change (`comparePreviousButton`, `compareNextButton`: both sides go to the change; a page added or removed shows
  the other side where it would be) and ✕ (`compareCloseButton`: ends the comparison and closes the reference).
- Versions cut out of the file are **read-only** wherever they are shown (`DocumentSession::isReadOnly`, the canvas
  reads only, the reference's edit switch is not offered), and they are not added to Recent (they are removed when the
  app quits).
- The comparison ends when the reference is closed or replaced, or either document is closed.
- **Not built:** on a changed page, what was added and what was removed highlighted (elements matched by their
  XML and `xqt-created`; `VersionDiff` keeps a signature per element for it). The scrubber across versions
  with ▶ (on the timeline's play bar).
