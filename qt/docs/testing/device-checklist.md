# On-device checklist (xournal-qt app)

Run from the build directory:
```sh
cd build-qt
./xournal-qt                     # new document
./xournal-qt ~/some/notes.xopp   # or a .xoj / .pdf
```

## Pen
- [ ] Writing: pressure visible, looks like Xournal++, no gaps when writing fast, ink follows the tip without lag.
- [ ] Highlighter: translucent, multiplies with what is underneath (text stays readable).
- [ ] Side button (eraser tool switch): erases while held; the pen draws again afterwards. The hover dot turns red.
- [ ] Eraser tool from the tool bar.
- [ ] Hand tool: dragging with the pen scrolls.
- [ ] Hover: the dot follows the pen above the screen.
- [ ] A stroke from one page into the next continues on the next page (split into two strokes, as in Xournal++).

## Touch and palm rejection
- [ ] One finger: pan; flick: momentum.
- [ ] Two fingers: pinch zoom anchored under the fingers. The page is sharp again about 0.3 s after the fingers stop.
- [ ] Two-finger tap: undo. Three-finger tap: redo.
- [ ] Palm on the screen while writing or hovering: the page never moves.
- [ ] Pen touching down while a finger pans: the pan stops and the pen draws.

## Document
- [ ] Undo/redo buttons and Ctrl+Z / Ctrl+Y.
- [ ] Add a page (+ page icon); scrolling through many pages stays smooth.
- [ ] Open a .xopp with a PDF background, and open a plain .pdf to annotate.
- [ ] Save / save as; the file then opens in upstream Xournal++ and looks identical.
- [ ] Closing or opening with unsaved changes asks first.
- [ ] Colors and the three sizes apply to pen and highlighter.

## Tabs (M5a)
- [ ] Open several files: each opens in its own tab; opening a file from the file manager while the app runs adds a tab to the running window.
- [ ] Switching tabs keeps each tab's zoom, scroll position and undo history.
- [ ] Closing a tab with unsaved changes asks first; quitting asks for every modified tab.

## Pages sidebar (M5b)
- [ ] The sidebar (first tool bar button) shows the pages; the current page is highlighted and follows scrolling.
- [ ] Tap a thumbnail (finger or pen): the canvas jumps to that page.
- [ ] Long-press or ⋮ on a thumbnail: insert before/after, duplicate, move up/down, delete. Each can be undone.
- [ ] After writing on a page, its thumbnail updates within about half a second, also after undo/redo.
- [ ] PDF pages show their PDF content in the thumbnail.
- [ ] Zoomed in, the page never draws over the sidebar.
- [ ] Page counter and zoom (−, %, +) in the pill at the bottom right; tapping the % fits the page width.
- [ ] Portrait (narrow window): the tool bar can be swiped sideways to reach all tools.

## Settings and tab overview (M5c)
- [ ] Gear button: the settings sheet opens; every section scrolls with a finger; sliders and switches work with pen and finger.
- [ ] Changing the pressure multiplier or the stabilizer changes the next stroke without restarting.
- [ ] Side button set to "Hand": the side button scrolls instead of erasing. Set back to "Eraser".
- [ ] Palm rejection timeout: with a long timeout, touch stays blocked for a moment after writing.
- [ ] Pinch zoom off: two fingers only pan.
- [ ] New pages: background/size/color from the settings are used by "add page".
- [ ] After closing and restarting the app, the settings are kept.
- [ ] Ctrl+Tab / Ctrl+Shift+Tab switch tabs.
- [ ] Grid button (or Ctrl+Shift+E): all open documents as cards; tapping one switches to it; × closes it (asks if unsaved).

## Crash recovery and session restore (M5d)
- [ ] Open two documents, quit normally, start again: both reopen, with the same current tab and page.
- [ ] Write in a saved document and in a new one, then kill the app (`pkill -SEGV xournal-qt`). Start again: "Recover unsaved changes?" lists both; Recover restores the ink (tab marked unsaved; Save writes to the original file).
- [ ] Same, but "Discard changes": the documents open as last saved.
- [ ] Settings → Documents → "Reopen the documents of the last session" off: the app starts with an empty document.

## Search
- [ ] Ctrl+F or the search button: typing searches the document (PDF text and typed text); hits are yellow, the current one orange; Enter / Shift+Enter and the arrows step through them and scroll.
- [ ] A big PDF: the count grows while searching ("…"), the app stays responsive.
- [ ] Tab overview: typing searches all open documents; documents with hits get an orange frame and a hit count, others are dimmed; tapping one opens it at its first hit with the search bar showing.
- [ ] Esc or × in the search bar clears the highlights.

Report problems with the input log (see qt/spikes/inkpad/README.md) or a screen recording.
