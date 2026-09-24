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
- [ ] Both side buttons of the pen erase while held (the default), also while drawing; the pen draws again afterwards. The hover dot turns red.
- [ ] Turning the pen around (eraser end) erases as well.
- [ ] Eraser tool from the tool bar.
- [ ] Hand tool: dragging with the pen scrolls.
- [ ] Hover: the dot follows the pen above the screen.
- [ ] A stroke from one page into the next continues on the next page (split into two strokes, as in Xournal++).

## Touch and palm rejection
- [ ] One finger: pan; flick: momentum.
- [ ] Two fingers: pinch zoom anchored under the fingers. The page is sharp again about 0.3 s after the fingers stop.
- [ ] Two-finger tap: undo. Three-finger tap: redo.
- [ ] Two taps with one finger on a page zoom in on what was tapped; on a paper with columns the column under the finger fills the view. Two taps again (zoomed in) show the whole page.
- [ ] A tap on a PDF link still follows the link and never zooms; a single tap alone changes nothing.
- [ ] Four-finger tap (touch screen): all pages of the document; again: back. Five fingers: all open documents; again: back.
- [ ] Four or five fingers pinched together ("zoom out") do the same; pinched apart they close the overview. The pages do not move while doing it.
- [ ] (A touch pad cannot do it: KWin keeps three- and four-finger gestures for the desktop.)
- [ ] Palm on the screen while writing or hovering: the page never moves.
- [ ] Pen touching down while a finger pans: the pan stops and the pen draws.
- [ ] Right after writing (pen lifted away from the screen), pinch zoom works at once; a hand resting on the screen while writing still does not move the page.

## Document
- [ ] Undo/redo buttons (in the page / zoom pill, also in full screen) and Ctrl+Z / Ctrl+Y.
- [ ] Add a page (+ page icon); scrolling through many pages stays smooth.
- [ ] Open a .xopp with a PDF background, and open a plain .pdf to annotate.
- [ ] Save / save as; the file then opens in upstream Xournal++ and looks identical.
- [ ] Closing or opening with unsaved changes asks first.
- [ ] Selection handles: round knobs, big enough for a finger; grabbing one a little beside it still resizes, the middle still moves. The red × removes the selection, the red knob at the right turns it.
- [ ] Colors and the four sizes apply to pen and highlighter.
- [ ] The fifth width (ring): tap it once to draw with it, tap it again (or press and hold) for the slider; − / + and the slider change the width live (mm, sample stroke); pen, highlighter and eraser each keep their own; it is still there after a restart.

## Tabs (M5a)
- [ ] Open several files: each opens in its own tab; opening a file from the file manager while the app runs adds a tab to the running window.
- [ ] Switching tabs keeps each tab's zoom, scroll position and undo history.
- [ ] Open a big, slow PDF, read a page and wait a few seconds: then scrolling forward (and a bit back) shows the
      pages at once, sharp, without rendering. More pages ahead are ready than behind.
- [ ] Open a big, slow PDF, wait ~10 s, then jump far (grid, sidebar or scroll bar): the page shows at once as a blurry
      preview (never white) and turns sharp when it is rendered.
- [ ] Close that PDF and open it again: the sidebar, the grid and far jumps show all pages at once (previews stored on
      disk, ~70 kB per page in ~/.cache/xournal-qt/pages). Write on a page, save, reopen: that page shows as saved.
- [ ] A library document opened for the first time shows its title page at once (the library's preview) while it
      renders.
- [ ] With the layers panel open, scroll through the document: the list of layers stays as it is and does not flicker;
      adding, hiding or renaming a layer still updates it.
- [ ] Drag the scroll bar of a big PDF quickly with the mouse and with a finger: the canvas follows without freezing;
      pages show their preview for a moment and turn sharp where the scrolling stops.
- [ ] While pages are being rendered ahead (right after opening or after zooming), scrolling and writing stay smooth
      and a page jumped to far away is not slower than before.
- [ ] Settings → Documents → Memory: "Rendered pages of the open documents" defaults to a quarter of the RAM and
      goes up to a third. Switch between two big documents: the one just left still shows its pages at once when you
      come back (unless the memory was needed for the other one).
- [ ] Closing a tab with unsaved changes asks first; quitting asks for every modified tab.
- [ ] A tap with one finger (or a middle click) on a tab only switches to it; only the × closes it.
- [ ] Menus opened by a finger or pen (tab menu, layout and zoom in the pill, tool bar menus, library menus) appear at the button or at the finger, not at the last mouse position.
- [ ] A right click does what press and hold does: layout and zoom in the pill, "add page", "mark PDF text" and the text tool.
- [ ] A menu without a close button (layout, zoom, tab, tool bar) closes when the page is touched - and that touch draws nothing. A tool tip still does not block drawing.
- [ ] The button beside the library tab opens all open documents; "Close all documents" there asks first and then closes them all (unsaved ones ask to be saved).

## Windows of their own (undocked documents)
- [ ] Right click (or press and hold) a tab → "Move to a window of its own": a second window opens with that document, the main window keeps the rest.
- [ ] Dragging a tab off the strip (downwards) does the same: the tab follows the finger and a hint at the top says what letting go will do (it lights up once far enough).
- [ ] The second window has no library tab; tools, colors, widths and settings are shared with the main window (changing a color changes it in both).
- [ ] In the second window: "Move to the main window" (menu or dragging the tab off the strip) puts the document back and closes that window when it was its last one.
- [ ] Close the second window with unsaved changes: it asks to save; whatever is left unsaved goes back to the main window instead of being lost.
- [ ] Copy pages in one window and paste them in the other.
- [ ] Kill the app (`kill -9`) with a changed document in the second window: after the restart it is offered for recovery (it comes back as a tab of the main window).

## Layers
- [ ] Sidebar → Layers: the layers of the current page, top first, "Background" last; the current one is marked and drawing goes there.
- [ ] "New layer" adds one above; tapping another layer draws on that one; the eye hides and shows a layer (the page follows at once).
- [ ] ⋮: rename, duplicate, move up/down, merge into the one below, delete. Each of them can be undone with Ctrl+Z.
- [ ] Switching pages shows the layers of that page; a document saved with several layers opens with them in Xournal++.

## Pages sidebar (M5b)
- [ ] The sidebar (first tool bar button) shows the pages; the current page is highlighted and follows scrolling.
- [ ] Tap a thumbnail (finger or pen): the canvas jumps to that page.
- [ ] At the end of the page sidebar and of the page grid: − N + and "Add pages" appends that many pages (like the current one; a PDF page is not copied), one undo step.
- [ ] Long-press or ⋮ on a thumbnail: a small pill with ten icons (copy, cut, paste, duplicate, delete, add a page,
      move up, move down, print, copy a link) over four lines (background, insert pages…, chapter, select all).
      Holding a finger on an icon names it. Each action can be undone, and the pill closes after it.
- [ ] After writing on a page, its thumbnail updates within about half a second, also after undo/redo.
- [ ] PDF pages show their PDF content in the thumbnail.
- [ ] Scroll the sidebar of a long PDF down and back up: the thumbnails seen before appear at once, without a blank
      moment. The same after switching to another tab and back, and when opening the page grid or the tab overview
      again.
- [ ] Open a PDF with 100+ pages, wait ~10 s, then fling the sidebar (and drag its scroll bar) to the end: every page
      shows at once as a small blurry preview, none is blank; where the list stops, the pages get sharp.
- [ ] The same in the page grid (also zoomed out to many columns) and in the tab overview.
- [ ] Right after opening a big PDF, scroll and write on the canvas: no stutter while the previews are prepared.
- [ ] Write on a page: its preview in the sidebar never goes blank, it shows the old state until the new one is drawn.
- [ ] Settings → Documents → Page previews: the memory slider (64–1024 MB, default 256) is kept after a restart.
- [ ] Zoomed in, the page never draws over the sidebar.
- [ ] Page counter and zoom (−, %, +) in the pill at the bottom right; tapping the % fits the page width.
- [ ] Portrait (narrow window): the tool bar can be swiped sideways to reach all tools.

## Settings and tab overview (M5c)
- [ ] Gear button: the settings sheet opens; every section scrolls with a finger; sliders and switches work with pen and finger.
- [ ] Changing the pressure multiplier or the stabilizer changes the next stroke without restarting.
- [ ] Settings → Pen: "Lower side button" / "Upper side button" set to "Hand": that button scrolls instead of erasing. Set back to "Eraser".
- [ ] Settings → Touch: "Touch waits after the pen" is 0.00 s: a finger scrolls the moment the pen is away (out of
      range); a palm resting on the screen while writing still does nothing. With e.g. 0.5 s, touch stays blocked
      that long after the pen left.
- [ ] Hover the pen over the Touch settings: if it tells its height, "The pen counts as near up to" and "Your pen
      is at … %" appear (else a hint says so). Set it to e.g. 30 %: with the pen held higher than that above the
      screen, a finger scrolls right away. "Take the pen's height": tap it, hold the pen at the height that should
      count as away for three seconds; the slider takes that height.
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

## Page grid
- [ ] Grid button (or Ctrl+Alt+G): all pages as a grid; flinging with a finger scrolls with momentum through long documents.
- [ ] Pinch apart / together: fewer, bigger / more, smaller previews per row; also −/+ and Ctrl+wheel.
- [ ] Tap a page (finger or pen): the grid closes at that page.
- [ ] With a search: hits are marked on the previews and in the sidebar (count badge, orange page frame); Enter in the search bar moves the grid along.
- [ ] "N pages with hits" (sidebar top, grid controls): only the pages with hits are shown in both; tapping one goes there; closing the search shows all pages again.

## Page layout
- [ ] Layout button (zoom pill, left): "Two pages side by side" shows pairs, fitted to the width; "Book" puts page 1 alone on the right; "Columns" −/+ shows N pages per row.
- [ ] Writing, erasing, scrolling, pinch zoom and the page counter work in every layout; strokes into the neighbour page continue there.
- [ ] The layout is kept after a restart and applies to all tabs.

## Shapes
- [ ] Shapes button: line, rectangle, ellipse, arrow, double arrow, coordinate system are drawn by dragging with the pen; Shift/Ctrl while dragging (keyboard) behave as in Xournal++ (square/circle, from the center).
- [ ] "Recognize shapes": a hand-drawn rectangle, circle, triangle or line becomes straight after lifting the pen.
- [ ] "Freehand" returns to normal writing.

## Selection
- [ ] Rectangle and lasso tools: drag around ink to select it (frame with handles); a tap on a stroke selects it; Shift adds.
- [ ] Drag inside the selection to move it (also onto another page); handles resize; the red handle rotates; × deletes.
- [ ] Copy / cut / paste (bar or Ctrl+C/X/V), also into another tab; Delete; Esc or a tap outside deselects; Ctrl+A selects the page.
- [ ] Undo/redo of moves, resizes, deletes and pastes.
- [ ] The side button set to "Lasso selection" in the settings selects while held.

## Text
- [ ] Text tool: tap on a page, type (hardware keyboard and the on-screen keyboard, also accents), Enter for new lines; the text looks the same while and after editing.
- [ ] Tap an existing text to edit it at the tapped place; arrows, Shift+arrows, dragging to select, Ctrl+C/X/V.
- [ ] Tap outside or Esc ends; undo removes/restores; emptying a text removes it.
- [ ] A text that contains a web address (pasted or typed) shows it underlined and blue; tapping it (hand or selection tool) opens the browser. Xournal++ shows the same text without the colour.
- [ ] ⋮ on a page → the link icon (the last one) gives "#Page:N"; pasted into a text it is a link that jumps there.
- [ ] Insert, delete or move pages in front of it: the number in the link follows; undo puts it back.
- [ ] Text button again (or long press): font family and size, also for the text being edited.

## Images
- [ ] Image button: choose a photo; it appears in the middle of the page, selected; move/resize it with the pen; undo removes it.
- [ ] Copy an image in a browser or screenshot tool, Ctrl+V on the canvas: inserted the same way.

## PDF links, export, back/forward
- [ ] In a PDF with links (e.g. a paper with references or a table of contents): tap a link with a finger: "Open" or "Go to page N"; writing over a link with the pen just writes.
- [ ] After a jump (link, page grid, sidebar), the ← → pill appears; ← returns to the exact place, → goes back to the target; Alt+Left/Right too.
- [ ] ⋮ → "Export as PDF…": the exported PDF opens in a PDF viewer with the PDF pages, ink, text and images.

## PDF text
- [ ] "Mark PDF text" tool, mode Highlight: drag over a line of PDF text: highlighted when lifting the pen (snapped to the text lines); undo removes it in one step.
- [ ] Modes Underline / Strike through (pen color); "Select by area" for columns.
- [ ] Mode Select: the bar offers Highlight / Underline / Strike through / Copy text; the copied text pastes elsewhere.

## Page operations
- [ ] Sidebar: Ctrl+click / Shift+click select pages (blue, check mark); a plain tap goes to the page.
- [ ] Ctrl+C, then Ctrl+V: the pages are pasted after the selection; pasting into another tab works (PDF pages keep their look).
- [ ] Delete removes the selected pages; the note at the bottom offers Undo. There is one undo for everything:
      Ctrl+Z (on the page, in the sidebar or grid) and the undo button of the pill take back the last change,
      ink or pages alike.
- [ ] Ctrl+N adds a page; Ctrl+Z takes it away again and a note says "Undone: Insert page"; Ctrl+Y brings it back
      ("Redone: …").
- [ ] Press and hold a page, drag it (or the selection) to another place in the sidebar or the grid: a bar shows the drop place, the view scrolls at the edges.
- [ ] Page grid on touch: "Select", tap pages, then Copy / Cut / Paste / Duplicate / Delete in the bar.
- [ ] Two-finger scrolling on the touchpad in the grid, sidebar and tab overview continues after lifting the fingers.

## Library and home screen
- [ ] "Last page" on the library (the same as "Open documents where they were left off" in Settings → Documents;
      off at first): a document closed at page 7 opens at page 7 again; off, it opens at its first page. Also after
      quitting the app.
- [ ] A document opened and closed again shows on its card (library and recent) a blue tag at the bottom of its
      preview: clock, when it was last read in the app, "p.N" of the page it was left at. Sort → "Last read first"
      puts the documents read last in front (never read ones at the end).
- [ ] The gear at the right end of the library opens the settings. At 1280 px width all buttons of that row are
      there (the search field gets narrower).
- [ ] "Names" beside the library search: on, only names are searched - of documents, and of folders in the folder
      view (the flat list shows none); a document that has the word only in its text is not found. Off: the full
      search again.
- [ ] `xournal-qt` without arguments: the home screen shows the library "Default" (folder created in Documents/Xournal_Libraries); `xournal-qt ~/some/folder` shows that folder as library, in its own window.
- [ ] Drag PDFs and .xopp files from Dolphin onto the library: they are copied (a note says how many); dropped on a folder card they go into that folder.
- [ ] Previews appear (first page, with annotations); "PDF ✎" marks annotated PDFs; a lone PDF shows up and opens for annotating; saving it creates the .xopp next to it and the card stays one item.
- [ ] New document: name, background, paper, landscape: it opens and the file exists in the current folder.
- [ ] Rename a document with a PDF: both files are renamed and the document still opens with its PDF (also when it is open in a tab; the tab title follows).
- [ ] New folder, enter it, breadcrumbs back; press and hold a card and drag it onto a folder or a breadcrumb: it moves there.
- [ ] Select several cards (circle, Ctrl/Shift+click, or "Select" in the menu then taps): Open opens them all as tabs; Copy to… / Move to… with the folder dialog; Trash.
- [ ] Library search: folders with the name (tap: opens the folder), documents with the text or the name, hit count and snippet; tapping a document opens it at the first hit.
- [ ] Import → "Import a folder with its subfolders…" (and dropping a folder): the whole folder tree appears in the library with its PDFs and .xopp files.
- [ ] Tab overview, extended search over the open documents: while the search runs and finds more hits, the page
      pictures already shown stay (they must not blink), and the cards keep their pictures when the overview reopens.
- [ ] Extended search (pages button next to the search field): every result shows its pages with hits (marked) under the title; swipe the row sideways; tapping a page opens the document at exactly that page with the search bar showing the hits. The pages appear quickly, also for long PDFs; scrolling the grid with the pen/finger over a page row still scrolls the grid.
- [ ] − / + (and Ctrl+wheel, pinch) in the library and recent grids: bigger or smaller cells.
- [ ] Recent tab: documents opened before; rename; select several and "Remove from list".
- [ ] Closing the last tab shows the home screen; the library tab switches to it with documents open; Ctrl+Tab returns.
- [ ] While a tool tip is shown (hover a tool button), the pen still writes on the canvas right away.
- [ ] Search one or two letters (document, tab overview, library): nothing happens while typing ("Enter ↵"); Enter or the search icon searches; a single letter in a long PDF stays responsive.
- [ ] Open 20+ documents: the tabs fill the whole strip before it scrolls, the + stays at the right end.

## Text mode
- [ ] Text mode button (or Ctrl+Alt+E): type "# Title", Enter, a paragraph, "- item", Enter, "item", Enter, Enter, text: the page shows it at the top left while typing (heading big, bullets with hanging indent).
- [ ] The editor is beside the page (right), not over it; the whole page width is visible next to it; closing it gives the old zoom back.
- [ ] H1/H2/H3, lists, B, I, A−/A+, colors (the same as the pen colors) act on the paragraph at the cursor (or the selected ones); Tab / Shift+Tab change the list level.
- [ ] Done; Ctrl+Z removes the whole text, Ctrl+Y brings it back; open the text mode again: the same paragraphs are in the editor.
- [ ] Writing with the pen over the text: the ink is on top; the text tool edits single text boxes.
- [ ] Save, open the file in Xournal++ (GTK): the same text (text boxes in a layer "Text").
- [ ] A long text: the warning says it does not fit.

## Downloads and other libraries
- [ ] Library menu (▾): "Downloads folder (quick library)" opens ~/Downloads in its own window, with a note that its files are short-lived; no .xournal_library folder appears in Downloads.
- [ ] In the Downloads library: Import (files, folder) and dropping files ask "Import into Downloads?" first.
- [ ] Menu of a card → Move to… / Copy to…: choose another library in the box at the top, then a folder: the document (with its PDF) or folder is there; moving into Downloads from another library asks first.

## Command line (scripts)
- [ ] `xournal-qt-cli --pdf-dir=OUT a.xopp b.xopp` writes OUT/a.pdf and OUT/b.pdf and says how many worked; a missing file is counted and the others still come out.
- [ ] `xournal-qt-cli FILE --create-pdf=OUT.pdf --export-range=2-5` writes those pages only.

## Selecting PDF text with a finger
- [ ] Hold a finger on a word of a PDF (or right click it): the word is selected and two knobs appear at its ends.
- [ ] Dragging a knob takes more or less text; letting go shows the pill with "Highlight", "Copy" and the rest.
- [ ] Holding the same word again selects its whole line.
- [ ] With the pen tools nothing changes: drawing over the text still marks it as before.

## Long press and right click on a page
- [ ] Holding a finger still on the page (or a right click) shows a small pill there: Paste, Select all, Image…; it stays inside the window near the finger.
- [ ] Paste puts text from anywhere (browser, terminal) on the page at that very place, images and copied elements as well; Ctrl+Z takes it back.
- [ ] With something selected the pill also offers Copy, Cut and Delete; on selected PDF text only Copy (nothing that would change the PDF).
- [ ] The long press does not draw and does not scroll the page.

## Chapters of a document without a PDF outline
- [ ] ⋮ on a page (or the ⋮ menu) → "Start a chapter here…": name and level; a heading appears at the top left of the page and the sidebar shows Pages | Layers | Contents.
- [ ] The contents list and the contents overview (Ctrl+Alt+O) show the chapters with their levels; tapping one goes to its page.
- [ ] Headings written in the text mode (H1-H3) show up as chapters as well; a text that starts with "# " does too.
- [ ] Ctrl+Z takes a chapter back; the file opened in Xournal++ shows the heading as ordinary text.

## Printing
- [ ] Ctrl+P (or ⋮ → Print…): asks what (with everything written on it, or only the PDF of an annotated document) and which pages; then the system's print dialog with the printers.
- [ ] "Print to file (PDF)" in that dialog writes the PDF where it was asked for.
- [ ] Select pages in the page overview → "Print": the dialog opens with exactly those pages (e.g. "1-3,5"); the same from ⋮ on a page.
- [ ] What comes out has the annotations (or not, if that was chosen) and only the chosen pages.

## Shortcuts
- [ ] Single keys take the tools: P pen, E eraser, H highlighter, T text, S select, L lasso, A hand, I insert an
      image. Not while typing (a text on the page, the search bar, a dialog) and not in the page grid or the
      overviews. F1 lists them under "Tools"; they can be changed like every shortcut.
- [ ] Overview of open documents: a saved document still on its title page shows at once, with the picture of the
      library (nothing is drawn); one scrolled to another page or changed shows that page as it is now.
- [ ] Long press / ⋮ on a page in the sidebar or the page grid → "Make it the title page": the library, the recent
      documents and the overview show that page for the document from now on ("✓ The title page" in its menu).
- [ ] Overview of open documents: the grid button beside the search shows under each document its pages with hits
      (marked); tapping one opens that document at that page. "Names" searches only the names of the open documents
      (the others are dimmed).
- [ ] Ctrl+Shift+F: the overview of all open documents opens with the cursor in its search. Ctrl+Alt+F (also from
      that overview): the library opens with the cursor in its search.
- [ ] F1 (or Ctrl+/) lists all shortcuts by group; Esc closes it.
- [ ] "Change…" opens the settings at "Shortcuts": tapping a row asks for keys; pressing e.g. Ctrl+Alt+P for "Add a page" makes that key work at once and Ctrl+N stop working.
- [ ] Ctrl+Shift+N (new document), Ctrl+Shift+E (all documents), Ctrl+Shift+L (library), Ctrl+Shift+S (save as) and Ctrl+Tab work - also with a second window open.
- [ ] A key another action already uses is marked in red with its name; Backspace in the dialog removes a shortcut; "Default keys" puts everything back.
- [ ] The chosen keys are still there after a restart.

## Tool bar, full screen, colors, pages
- [ ] Eraser button: a tap takes the eraser; a second tap, holding it or a right click offers "Standard", "Whole
      strokes", "Whiteout"; the chosen one is ticked and erases that way.
- [ ] Shapes menu → "Snap to the grid" (off at first): on, moved selections and shape corners jump onto the
      half-centimetre grid; off again, they follow the pen exactly.
- [ ] ⋮ → Tool bar position → Left / Right: the tools in a column at that side (scrolls if the screen is too low); back to Top.
- [ ] The small tab with the arrow at the end of the tool bar puts it away (the small tool square appears, as in full screen); the slim strip at the top edge brings it back. ⋮ has both as well, and the choice survives a restart.
- [ ] With the bar at the left or right: the tab sits at its inner edge and works the same.
- [ ] Without a tool bar (hidden or full screen) and with pen or highlighter: a small pill at the side has the pen/highlighter switch, three colors, "+" for more (press and hold one to remove it) and a width knob.
- [ ] The width knob: every tap takes the next of the five widths of the tool bar (the fifth as set there), then the first again; the dot shows how thick it is.
- [ ] Drag the pill to another side of the screen: it stays there, also after a restart.
- [ ] F11: only the tool square and the page / zoom pill are left; drag the square; tap it: tools, colors, sizes; choosing one closes them; the pen works right away; Esc leaves full screen.
- [ ] Colors: the Xournal++ palette without white (black, green, light blue, light green, blue, gray, red, magenta, orange, yellow); "+" adds a color from the color dialog; press and hold a color: remove it; "Default colors".
- [ ] Mark PDF text: the three highlight colors in the tool's menu and in the select bar; highlights come in the chosen color.
- [ ] ⋮ on a page (or ⋮ menu → "Background of this page…"): choosing graph paper changes that page (or every selected page), one Ctrl+Z in the sidebar puts it back.
- [ ] On a page of an annotated PDF the dialog warns that the PDF page is replaced; what was drawn stays.
- [ ] Press and hold "add page" (or ⋮ → Insert pages…): 3 graph pages, landscape, after this page; one undo in the sidebar removes them.

## Pages in the overview and the sidebar
- [ ] Page overview → "Select": tap several pages one after another - every tapped page gets the check mark and
      they all stay selected, also when a tap rests a moment on the page.
- [ ] Press and hold a page and let go without moving: it is only selected, no page moves (nothing to undo).
- [ ] Press and hold a page and then move: the pages follow the finger and are dropped where the bar shows.

## PDF text selection
- [ ] Hold a finger on PDF text: the word is selected with two knobs; the pill offers Copy and the marking colors.
- [ ] Tap (or press with the pen) somewhere else on the page: the selection and its knobs are gone, and that press
      does not draw. Everything works right away again: pen, scrolling, pinch zoom, tools.
- [ ] Copy from the pill: the text is in the clipboard, the knobs are gone and the canvas takes input as before
      (this used to freeze the canvas).
- [ ] Scroll and zoom while text is selected: the knobs and the pill stay on the text and follow it.
- [ ] Scroll on until the text is out of sight: the pill waits at the top edge of the canvas with an arrow (up or
      down, whichever way the text lies); tapping it goes back to the text, still selected.

## Selection with a finger
- [ ] Select strokes or an image, then move the selection with a finger inside it: it follows; the page does not
      scroll away under it.
- [ ] Pull a corner or a side handle with a finger: the selection is resized; the round handle above it turns it.
- [ ] A finger beside the selection scrolls as usual, and pen and mouse work as before.

## Setsquare and compass
- [ ] Shapes tool → "Setsquare": the triangle with its scales lies in the middle of the page; drawing with the pen
      on it or along one of its three edges gives a straight line along the nearest edge, away from it a free line.
- [ ] The pen and the left mouse button never push the tool around, they always draw.
- [ ] The eraser end of the pen is not guided: it rubs out what lies under the tool, not only along its edge.
- [ ] Two fingers on the tool carry it along, turn it and size it (the page keeps its zoom and place); one finger
      on it scrolls as everywhere else; two fingers next to it zoom as always.
- [ ] Carrying it with two fingers does not turn or size it a little on the way; turning it keeps under the fingers
      what was under them; it follows smoothly, also turned all the way around, and without lag.
- [ ] Lift one of the two fingers and move the other: neither the page nor the tool moves. Put the finger back
      anywhere: nothing jumps, and the two carry on.
- [ ] A quick two-finger turn or push of the tool never undoes the last stroke.
- [ ] Right mouse button on the tool, drag down onto the next page: it follows the pointer all the way.
- [ ] Move the tool's page elsewhere in the sidebar: the tool stays on it. Delete that page: the tool is put aside
      (small pill); tapping the pill brings it onto the page one is at. Close the tab with the tool out: no crash.
- [ ] Right mouse button pressed on the tool and dragged: it moves along (no menu appears); beside the tool the
      right button offers the actions as before.
- [ ] Shapes tool → "Compass": drawing anywhere on its disc gives an arc of its radius; the same entry again takes
      the tool away.
- [ ] With the tool out, a pill sits at the top right of the canvas. "15°" on: turning it with two fingers goes in
      steps of 15 degrees (the fingers turn on freely, the tool follows step by step); off: freely again.
- [ ] The magnet in the pill: the middle of the setsquare (the 0 of its scale) jumps onto the nearest ink stroke.
      Moving it with two fingers (or the right mouse button) now slides it along that stroke - also along a curve -
      and never off it, so lengths can be read along the line. A second tap on the magnet lets go. On a page
      without ink it says so and stays free.
- [ ] The ruler in the pill draws the marks of the setsquare's scale onto the page with the pen's color and width:
      short lines on the paper beside the long edge, every centimetre (whole ones a little longer). "1 cm" next to
      it switches to every half centimetre and back. One Ctrl+Z takes all marks of one tap away. Not offered for
      the compass.
- [ ] The setsquare icon in the pill puts the tool aside: it is gone from the page and does not guide the pen, the
      pill shrinks to the icon. A tap on it brings the tool back where it lay (on the page one is at now if that
      spot is on it). The Shapes entry takes tool and pill away altogether.

## Markdown boxes
- [ ] Hold the text mode button (or right click): "Markdown"; or Ctrl+Alt+M. The editor opens beside the page; type
      "# Notes", Enter, "- one", Enter (the list goes on), "two", Enter, Enter (the list ends), "Some **bold**": the
      page shows it formatted while typing (heading with a line below, bullets, bold), at the top left margin.
- [ ] The buttons: H1-H3 and the list / quote buttons change the line's mark; B, I, S, code, Link put marks around
      the selection. Ctrl+B / I / E / K, Ctrl+1-3; Tab / Shift+Tab indent a list item.
- [ ] Done; the button now shows the Markdown icon (a tap opens Markdown again). Ctrl+Z removes the whole text,
      Ctrl+Y brings it back. Cancel puts the page back as it was.
- [ ] Zoom in far: the text stays sharp. Thumbnail and PDF export show it formatted; in the exported PDF the text
      can be selected.
- [ ] Text tool: a tap on the box opens its Markdown (not a text box with the source). A tap on a link in the box
      (text tool off) offers to open it; `[see](#Page:2)` goes to page 2.
- [ ] Headings 1-3 of a box are chapters in the contents sidebar.
- [ ] Search (Ctrl+F) for a word of a box: the mark is on the drawn word (not somewhere in between).
- [ ] A code block with ```` ```python ```` (or cpp, js, bash): keywords, strings and comments in colors.
- [ ] Size: "Size" in the editor changes the text (and the size of new Markdown text); new text starts at 60 % of
      the text font (16 pt: 10).
- [ ] Text tool, hold its button: switch "Markdown" on; the size there is the Markdown size. Tap anywhere, type
      "Some **bold**", Esc: drawn formatted where it was tapped. Tap it: the source again, on the page. Markdown off:
      ordinary text boxes again. The pen still writes into its layer.
- [ ] Text tool, Markdown on, "Write Markdown beside the page" on: tap anywhere: the editor opens ("Markdown text box
      on page …"); the page shows the box formatted while typing, where it was tapped. Done; a tap on it opens it
      again; Ctrl+Z removes it in one step. Off: written on the page (the source while editing).
- [ ] Writing on the page (the default; font menu: "Write Markdown beside the page" off): tap the page's Markdown
      text or a Markdown box with the text tool: the cursor is where tapped; type "# Title", Enter, "Some **bold**":
      the heading is drawn big at once, the line being typed shows "**" grey around bold "bold". Tap the heading:
      it shows its "# " and the line before is drawn formatted. Arrows, Up / Down over the lines as drawn, Shift to
      select, Ctrl+B around a word, Ctrl+Z / Ctrl+Shift+Z, Enter in a list (next item; on an empty item it ends),
      Tab / Shift+Tab. Escape: done; Ctrl+Z removes it all. Long page text: it flows onto new pages while typing and
      the view follows the cursor. Ctrl+Alt+M: the same text beside the page. The on-screen keyboard types into it.
- [ ] Task list "- [ ] milk": a tap on its box (hand tool, finger, or the text tool) ticks it, again unticks it;
      Ctrl+Z. While writing on the page (cursor in another paragraph): a tap on the box ticks it, the cursor stays.
- [ ] Select (rectangle or lasso) around a Markdown box: the frame is the drawn box; drag it (formatted while
      dragging), also onto the next page; Delete; Ctrl+Z. After the selection the pen writes into its layer again.
- [ ] Save, open the file in Xournal++: the page shows the Markdown source as a text, nothing lost; saved there and
      opened again here: formatted again.
- [ ] Longer than the page: it flows onto new pages while typing (the title says "pages 1–3"); a heading at the
      bottom of a page moves to the next page with its paragraph; a long code block continues on the next page
      with its colors; a long table repeats its header. Deleting text: the pages added go again. Done, Ctrl+Z:
      text and pages go in one step. Open the text on page 2 (text tool on it): the whole text. Save, open in
      Xournal++: every page shows its part of the source.

## Table of contents
- [ ] A PDF with bookmarks (e.g. a book or thesis): sidebar "Contents": indented headings, current section highlighted, tap goes there, arrows collapse.
- [ ] Contents overview (toolbar button or Ctrl+Alt+O): chapters big, sections smaller and indented, the pages of each section in a row (swipe sideways); tap a page: it opens there; "Chapters only"; − / +.
- [ ] The grid button next to the page number in the pill opens the page grid.

## Package
- [ ] `sudo apt install ./build-deb/packages/xournal-qt_0.1.0_amd64.deb`: Xournal Qt in the application menu with its icon; PDFs and .xopp files offer "Open with Xournal Qt"; right click on a folder in Dolphin: "Open as Xournal Qt library" opens it in its own window.

Report problems with the input log (see qt/spikes/inkpad/README.md) or a screen recording.

## Tab arrows, full-screen button, docked tool bar, pen paste pill (qt/ui-polish)
- [ ] ⋮ → Tool bar position → Left: the little tab at the bar's edge points left (towards the bar). Tap it: the bar
      goes, a slim strip stands at the middle of the left edge with its arrow pointing right; tap it: the bar is back.
      The same with Right (tab points right, strip at the right edge pointing left; the scroll bar sits beside the
      strip, not under it) and Top (tab points up, strip at the top pointing down). With the pen, the pen pill at
      the right edge does not cover the strip.
- [ ] One document open: no arrows beside the "all open documents" button. Open three: ‹ and › appear right of
      it; › goes to the next document and from the last one to the first (like Ctrl+PgDown), ‹ the other way. On
      the home screen, either arrow goes back to the document behind it (like the shortcut). Touch sized with a finger.
- [ ] Tool bar (top, left and right): the full-screen button (four arrows) beside Search / Settings goes full
      screen. Back out by finger only: tap the small tool square, then "Leave full screen" at the bottom of its
      tools. The button is not repeated in those tools. F11 and Esc still work.
- [ ] Copy something (a text in another app, or elements with Ctrl+C). Pen tool, hold the pen still on an empty spot
      of the page for about half a second: the pill (Paste, Select all, Image…) appears there, the dot the pen began
      disappears, moving the pen before lifting draws nothing. Paste puts it at that spot. The same with the
      highlighter and the hand. Writing normally, pausing on the page mid-word, putting dots (i, full stops) and
      short slow strokes: no pill, nothing lost. The eraser held still only erases.
- [ ] A PDF with text: hold a finger (then the pen) on a word: the word is selected and its pill (highlight,
      underline, copy…) also has Paste at the end; Paste unselects the text and puts the clipboard where you held.
      With an empty clipboard there is no Paste. Text selected by dragging with the PDF text tool: no Paste.

## Rendering while zooming and closing (qt/render-visible)
- [ ] A big PDF (or a long .xopp), one page in view: pinch in by steps and stop each time; Ctrl+wheel in and out.
      Each time the page goes sharp about a third of a second after the fingers stop and never turns into a grey
      page; zoomed out to 3–4 pages in view, all of them come back sharp too. Pages that were only blurry (their
      preview) while flying through the document get sharp once the view stands still.
- [ ] Pinch in and lift the fingers: the page goes sharp right away (no third of a second wait); Ctrl+wheel still
      waits a moment after the last notch. With `XQT_PERF=1`, the log's `sharp N after a/b ms` shows how long the
      pages in view waited for their render; note the numbers for a big PDF zoomed in and out.
- [ ] Right after opening a big PDF (previews being drawn, sidebar open), zoom and scroll: the page in view gets
      sharp first; the sidebar thumbnails and the previews follow after it.
- [ ] A big PDF with the sidebar or the page grid open (many thumbnails asked for), zoomed in: close its tab, then
      close the app with such a tab open. Both happen at once (at most the page being drawn is waited for); before,
      the window froze for seconds.

## Markdown code blocks and search boxes (qt/markdown-fixes)
- [ ] Markdown written on the page, at the end of the text: type ```` ```py ````, Enter, `some code`, Enter,
      `#stuff`, Enter. Each Enter starts one new line of the code (no blank line in between), and the grey background
      follows. Type ```` ``` ```` and Enter: the code block is shown finished (fences hidden, grey ends at its last
      line), the cursor is below it, outside the grey. Type a word: it appears at once where the cursor was, and the
      cursor moves with every letter; Enter and another word as well. No need to type more or open the source beside
      the page.
- [ ] A document with the page's Markdown text flowing over two pages: a heading, **bold** and *italic* words, a
      list, inline `code` and a code block, and a word that appears in each of them and on the second page. Search
      (Ctrl+F) that word: every yellow box sits on the formatted word (the big heading word, the word after hidden
      `**`, the list item right of its bullet, the grey code), on both pages, in the sidebar and in the page grid too.
      Search two words that the paragraph breaks between lines: two boxes, one on each line (not one big box).
- [ ] Write on the page (the text tool on the text), the cursor in a paragraph with `**bold**` in it; open the search
      and search a word after the bold one: its box is on the word as shown with its `**`. Press Escape: after a
      moment the box moves onto the word as formatted.

## Window state (qt/window-state)
- [ ] The app opens maximized (also a window made by undocking a tab). Making it smaller with the desktop's tiling
      works as usual.
- [ ] Maximized, F11 (or the full-screen button), then Escape: back to maximized, not the small default size. The
      same by "Leave full screen" in the tool square.
- [ ] Made smaller (not maximized), full screen, Escape: back to that size.

## Per-folder library cache (qt/library-index)
Try these on a **copy** of a library, or on a new one. Opening a real, cloud-synced library with this build changes its
cache on disk (and, once the whole block is in, converts the old one), which the sync client then uploads.
- [ ] Open a library with documents in several folders and wait until the search finds text in them. Each folder
      with documents now has a hidden `.xournal_library/` with `notes.pack` (and `pdf-text.pack` where there are
      PDFs); folders without documents have none. Close and open the library again: the search works at once and
      nothing is indexed again (no progress shown).
- [ ] Write a text element into a `.xopp` and save it. A few seconds later only that folder's `notes.pack` has a new
      time (`ls -la --time-style=full-iso <folder>/.xournal_library`); `pdf-text.pack` and the other folders' packs
      keep theirs. The new text is found.
- [ ] In the file manager, rename a folder of the library and move a document into another folder. Back in the app:
      both are found by their text right away, nothing is indexed again.
- [ ] Open a subfolder as a library of its own ("Open a folder as library…"): its search works at once.
- [ ] Trash all documents of a folder in the app: its `.xournal_library/` goes too.
- [ ] A folder with many documents: the cards show their previews (drawn once); each folder's `.xournal_library/`
      gets one `previews.pack` instead of a PNG per document. Close and open the app: the previews are there at
      once, nothing is drawn again. Choose another title page of a document: its card shows that page.
- [ ] Rename a PDF in the app: its card keeps its preview (not drawn again).
- [ ] Reading positions: open a document at some page, close it, close the app. `~/.config/xournal-qt/libraries/`
      has a folder for the library with `pages.json`. Delete every `.xournal_library/` of the (copied) library by
      hand and open it again: the cards still show when and at which page each document was read.
- [ ] Converting an old cache: make a copy of a library that was used with an older build (its root has
      `.xournal_library/index/`, `previews/`, `pages.json`) and open the copy. **Opening a real, cloud-synced library
      converts it for good** (the old files are removed and the sync client uploads that). After a few seconds: `index/`,
      `previews/` and `pages.json` are gone from the root's `.xournal_library/`, each folder has its packs, no
      progress of indexing was shown (nothing read again), the search finds text at once, the cards show their
      previews without drawing them again, and the cards still show when and at which page documents were read.
      Note how long the conversion took for a big library (the packs appear one folder after the other).
- [ ] Settings → Storage (on the copied library): it shows what the cache takes ("It takes … in … files"). Switch on
      "Keep the cache in the app's cache folder": the `.xournal_library/` folders disappear from the library's
      folders and appear under `~/.cache/xournal-qt/libraries/<key>/…`; the search and the previews still work at
      once (nothing is indexed or drawn again). Close and open the app: still on. Switch it off: the folders are back
      in the library, the app-cache folder of the library is gone.
- [ ] With the cache in the app cache folder, close the app, rename a folder of the library in the file manager and
      open the library again: its documents are found by their text at once, nothing is indexed again; the folder's
      old place under `~/.cache/xournal-qt/libraries/<key>/` is gone.
- [ ] Settings → Storage → "Remove all cache folders of this library…": the dialog says the app closes. With an unsaved
      document open, it first asks to save it. After OK: no `.xournal_library/` left in the library (a file of your
      own put into one beforehand stays, with its folder), and the app is closed. Open the library again: the cache
      is built again, the cards still show where you were in each document.

## Pasted PDF pages stay searchable (qt/pdf-pages)
- [ ] Open a lecture PDF with its `.xopp` (a document of the library) and another PDF with text. In the other PDF,
      copy a page (sidebar or page grid, Ctrl+C), go to the lecture and paste it (Ctrl+V). The note says the page
      was pasted and that its PDF pages are saved in `.lecture.pages.pdf` next to the document; it is readable and
      wraps in a narrow window. The pasted page looks as in the other PDF (sharp at any zoom, not a picture).
- [ ] Search (Ctrl+F) a word of the pasted page: it is found, marked on the page, in the sidebar and in the page grid.
      Mark PDF text on the pasted page (select, copy text, highlight): works as on the lecture's own pages.
- [ ] The lecture's own PDF (`lecture.pdf`) is unchanged: same size and date in the file manager.
- [ ] Paste pages from two more PDFs, and the same page twice: the folder has one `.lecture.pages.pdf` (show hidden
      files), no other new files. Undo the pastes and redo them: the pages come and go as before.
- [ ] A document without PDF (a new document, saved): paste a PDF page. A `name.pdf` appears next to the `.xopp`,
      and the library still shows one card for the document.
- [ ] A new document, never saved: paste a PDF page. It shows and its text is found; the note says the pages are
      saved next to the document. The tab is still "Untitled".
- [ ] The new document with the pasted page: save it into the library. `name.pdf` appears next to it (or the hidden
      `.name.pages.pdf` if that name is taken); close and reopen it: the page shows and its text is found.
- [ ] A lecture with pasted pages: delete a pasted page and a page of the lecture, save. `.lecture.pages.pdf` gets
      smaller (file manager, hidden files shown); the document still shows the right pages, also after reopening,
      and in Xournal++ (upstream) if it is installed. Undo the two deletions: both pages come back with their PDF
      text; save again.
- [ ] Save without changes: `.lecture.pages.pdf` keeps its date (not written again).
- [ ] In the library, the lecture with pasted pages is one card (no `.lecture.pages.pdf` card). Rename it, move it
      into a folder, copy it to another library, open each: the pasted pages show and their text is found. The
      library search (also the extended search with pages) finds a word of a pasted page. Move it to the trash: the
      trash has `.lecture.pages.pdf` with it.
- [ ] Rename the lecture while it is open with a pasted page not saved yet, then save: the pages stay right.
- [ ] Paste a page from another PDF into a saved lecture: no `.lecture.pages.pdf` appears next to it (hidden files
      shown) until you save. Close it without saving: nothing new next to it, and nothing left in
      `~/.cache/xournal-qt/pasted-pages`.

## Fast search in open documents (qt/document-search)
- [ ] Ctrl+F in a long PDF, type a word quickly, key by key, then pause and type on: every letter stays in the field
      (it used to be set back to the text searched before whenever results came in). The count is that of the text in
      the field. The same in the tab overview's search and in the library's search.
- [ ] Open a long PDF of the library (e.g. a manual of a thousand pages) and press Ctrl+F right away: while typing,
      the count and the marks in the sidebar come with every key (no "Searching…"); the canvas scrolls to the first
      hit from the current page on, and its marks are there. Enter / Shift+Enter step through the hits, also onto
      pages far away: each is marked where the word is.
- [ ] The same with a long PDF that is not in the library (e.g. from Downloads opened with "Open"): the count grows
      for a few seconds ("…" behind it), scrolling and drawing stay smooth meanwhile, the hits near the current page
      come first. Search again later: all counts at once.
- [ ] A phrase across a line break of the PDF (the last word of a line and the first of the next) is found and marked
      on both lines; a word broken with a hyphen at a line end is found when typed without the hyphen. The count
      next to the field matches the marks on the pages.
- [ ] Write a text or a Markdown box with a searched word while the search is open: after a moment the count and
      the marks include it (without saving); undo takes it away again. Delete, insert and move pages: the pages with
      hits follow.
- [ ] Tab overview → search all documents: the counts per document come at once for the documents of the library;
      the extended view marks the hits on the first pages.
- [ ] Save a document of the library after adding a text: the library search finds the new text at once and the
      library shows no indexing progress for it.

## Hybrid PDF (qt/hybrid-pdf)
- [ ] Open a lecture PDF, write with pressure, highlight a line of its text, add a text box, add a ruled page. More →
      Save as hybrid PDF…: it suggests `lecture.notes.pdf` next to the lecture. Save. The tab is called
      `lecture.notes.pdf`; `lecture.pdf` is unchanged (size and date).
- [ ] Open `lecture.notes.pdf` in other PDF apps: **Acrobat, Preview (macOS/iPad), Xodo, Drawboard, Chrome (pdf.js
      in Chrome/Firefox), Okular, Evince**. For each note: the ink looks like in xournal-qt (pressure widths, not one
      width per stroke); the highlighter is see-through and the text under it stays readable (multiply); the text
      box and the ruled page show; the lecture's own bookmarks and links still work. Note which apps differ.
- [ ] In those apps: select a stroke layer (one annotation per layer and page), move it, delete another, add a
      comment of your own, save. Reopen in xournal-qt: it says the PDF was edited in another app. "Keep the Xournal
      data": the ink is where it was, your comment shows; save, reopen in the other app: the comment is still there.
      Do it again and choose "Import": the moved ink shows where the other app put it (not editable), Undo brings the
      editable layer back.
- [ ] Which apps keep the attachment `document.xopp` when they save (the file opens in xournal-qt with editable
      strokes afterwards), and which drop it (it then opens as a plain PDF with the ink as annotations)? Which save
      incrementally (file only grows) and which rewrite it?
- [ ] Ctrl+S on the hybrid PDF saves it again without asking (the date changes); close and reopen: all strokes are
      editable, layers and text boxes as they were.
- [ ] More → Export as .xopp for Xournal++…: `lecture.notes.xopp` appears (and hidden `.lecture.notes.pages.pdf`).
      Open it in upstream Xournal++ if installed: the pages and the ink show once, not twice. In the library the
      hybrid PDF and its `.xopp` are one card, which opens the hybrid PDF.
- [ ] Settings → Documents: turn on "Save notes into the PDF itself": an explanation shows once (not when turned
      off and on again). Open another PDF, write, Ctrl+S: no dialog; the PDF itself now has the notes, and
      `name.original.pdf` next to it is the old file. Write more, Ctrl+S again: `name.original.pdf` does not change.
- [ ] Turn on "also write a .xopp for Xournal++": every Ctrl+S on a hybrid PDF also writes `name.xopp`.
- [ ] A `.xopp` document and an unsaved new document still save as `.xopp` with Ctrl+S / Save as (nothing changed).
- [ ] Library search finds a word of a hybrid PDF's pages and a word of one of its text boxes.
- [ ] Time Ctrl+S on a hybrid PDF of a long PDF (several hundred pages) with notes on a few pages: acceptable?

## Markdown files and images in the library (qt/library-files)
- [ ] Open a `.xopp` with an image as page background stored with it (upstream: page background → image, "attach";
      next to it `name.xopp.bg_1.png`), rename it in the library, move it into a folder, copy it to another library:
      each time it opens with its background image, and no `.bg_1.png` is left behind at the old place. Trash takes
      the image along.
- [ ] Put `notes.md`, `board.png`, `photo.jpg` and a phone photo `.heic` (if Qt can read it here) into a library
      folder: each is a card ("MD", "IMG"), also in "All documents" and in "Recent" once opened. A `.txt` stays hidden.
- [ ] Rename `photo` in the library while `photo.xopp` is next to `photo.jpg` (one card, "IMG ✎"): both files get
      the new name, and the `.xopp` still shows the photo. Move it into a folder and copy it to another library: the
      same. Trash takes both.
- [ ] Rename, move, copy and trash a Markdown file card; import a folder with `.md` files and images: they come along.
- [ ] The card of a Markdown file shows its first page as an A4 page with the text formatted (headings, lists,
      tables, code). An image card shows the image; a phone photo taken upright is upright, also a `.heic`.
- [ ] Search the library for a word of a Markdown file (in a heading, a paragraph, a list, a table, a code block): the
      file is found, with the text around the hit on its card. Its Markdown syntax (`**`, `#`, link targets) is not
      found. Edit the file in another editor and save: after a moment the new text is found, the old one not.
- [ ] Tap a Markdown card: it opens as A4 pages with the text formatted, the tab titled `notes.md`, and a note at the
      bottom left says it is read-only for now. Scroll, zoom, search (Ctrl+F) in it. Try to write with the pen, the
      highlighter and the text tool: nothing is written, the page scrolls instead, a tap on a link still opens it.
      Close it: no question about saving, and the `.md` is unchanged.
- [ ] Tap an image card (a whiteboard photo): it opens as one page with the photo as background, titled `photo.jpg`,
      with a note that saving keeps it as `photo.xopp`. Write on it, save: the dialog suggests `photo.xopp` next to the
      photo. Back in the library the two are one card ("IMG ✎") that opens what you wrote. The same with a photo from
      the phone taken upright (it stays upright after saving and opening again, also in Xournal++) and a `.heic`.
- [ ] Extended library search (pages button) for a word in Markdown files: a Markdown result shows a row of cards
      instead of pages, each the paragraph / list item / table row / code block with the hit, formatted, the headings
      above it in small italics, the first hit orange, the others yellow; a long code block is cut around its hit.
      A PDF or `.xopp` result next to it still shows its pages. Tap the second card: the file opens at the page of
      that passage, with that hit current in the search bar (e.g. "2 / 2"), and the read-only note at the bottom left.
      Swipe the row sideways with a finger.
- [ ] "Open" (Ctrl+O) and the library's "Import" offer Markdown files and images among the documents.

## Page grid while searching (qt/grid-search-scroll)
- [ ] A long PDF, search a word with many hits, open the page grid (filter on and off) and scroll down slowly and
      fast: the grid stays where you scroll. Enter in the search bar still moves the grid to the next hit.

## Show filter and other files (qt/library-filter)

- [ ] Put into a library folder: `thesis.tex`, `kalman.py`, `notes.txt`, `Makefile`, `report.docx`, `budget.xlsx`,
      `slides.pptx`, `archive.zip`, `song.mp3`. With the default "Show" filter none of them is a card, the folder's
      card counts only its documents, and the library looks as before.
- [ ] The "Show" button (funnel, next to the sort button) opens a list of toggles: Notes, PDFs with "Only PDFs with
      notes" below it, Markdown, Images, Text and code, All other files. Turn on "Text and code": the `.tex`, `.py`,
      `.txt` and `Makefile` are cards ("TEX", "PY", "TXT") showing their first lines in a monospaced font. The button is
      marked. The popup stays open while toggling; a tap outside closes it.
- [ ] Turn on "All other files": the Office files and the rest are cards with an icon of their type (document,
      spreadsheet, slides, archive, audio), the extension as badge, the whole file name, size and date. Folder cards
      count them now.
- [ ] "Only PDFs with notes": lone PDFs go, PDFs with their `.xopp` stay, a hybrid PDF (also alone) stays. Turning
      "PDFs" off greys the sub-toggle.
- [ ] Close the window and open the library again: the filter is as you left it. Another library has its own
      (the default one for a library never changed). "Defaults" in the popup brings the default back.
- [ ] Tap a `.py` card: it opens read-only as A4 pages, monospaced, with colors for the code (if KSyntaxHighlighting is
      built in), titled `kalman.py`, the read-only note at the bottom left. The pen does not write; Ctrl+F finds text
      in it. A `.txt` has no colors. A text file with a line of backticks in it is shown whole.
- [ ] With "Text and code" on, search the library for a word inside the `.py`: it is found, the card shows the text
      around the hit ("Found in the text" in the extended search). Turn "Text and code" off: it is not found any more.
      A text file over 1 MB is found by its name only.
- [ ] With "All other files" on, search for part of `report.docx`'s name: it is found (before documents that only
      contain the word). Its text is never searched.
- [ ] Tap `report.docx`: LibreOffice (or the app set for it) opens it; no tab opens. Its menu (⋮, right click, press
      and hold) has "Open with the system app" and "Show in file manager": Dolphin (or the file manager) opens the
      folder with `report.docx` selected. A text file's menu has "Open with the system app" too (the editor set for
      it); a PDF's has not.
- [ ] Rename `report.docx` (the dialog shows the whole name; change it to `Report 2026.docx`), move it into a folder
      with a `Report 2026.docx` already there (it becomes `Report 2026 (2).docx`), copy it to another library, trash
      it. The same for a text file.
- [ ] Import a folder with `.docx` and `.py` files while "All other files" / "Text and code" are off: only the
      documents come along. With them on: they come along too.
- [ ] Android (later): "Show in file manager" is not in the card menu; tapping an Office file offers the apps for it.
- [ ] Open a folder outside `Documents/Xournal_Libraries` as library ("Open a folder as library…", or
      `xournal-qt ~/some/folder`, or Dolphin's action): in any window's Recent grid it is a folder card with the
      library mark, its name and path, among the recent documents by time. Tap it in another window: that library's
      window comes to the front (no second window). Close that window and tap again: it opens. Its menu has no
      Rename, Copy, Move or Trash. Delete or rename the folder in Dolphin: it drops out of Recent. Libraries in
      `Xournal_Libraries` (Default, …) do not appear there.
- [ ] In the library's folder view, a folder card's menu (⋮, right click, press and hold) has "Open as library (new
      window)": a new window opens with that folder as library, its cards appear at once with their previews, and
      the search finds its documents without "Indexing for search" running through them again.

## Preview writes (qt/preview-writes)
- [ ] In a library folder synced by OneDrive (cache in the folders), open a `.xopp` with a few pages, write on page 3,
      save, go back to the library and wait ~5 s: the card shows the same preview. `ls -l --time-style=full-iso
      <folder>/.xournal_library/`: `previews.pack` keeps its time; `notes.pack` and a small `preview-stamps.pack`
      (well under 1 KB) are new. The sync client uploads only those.
- [ ] Close and open the library again: the card shows its preview at once (not drawn again), `previews.pack`
      still keeps its time.
- [ ] Write on page 1 and save: the card shows the new first page, `previews.pack` is written again (new time) and
      `preview-stamps.pack` is gone.
- [ ] Make page 2 the title page ("Make it the title page" in the page grid): the card shows page 2 and
      `previews.pack` is written. Then edit page 4 and save: `previews.pack` is left alone; edit page 2: it is written.
- [ ] Settings → Storage → Clean-up still removes everything (also `preview-stamps.pack`); switching the cache to
      the app cache moves it along.

## Saving in the background (qt/background-save)
- [ ] Open a long PDF (pgfmanual, or a lecture of several hundred pages), write a few strokes, save it as a hybrid
      PDF, then write more and press Ctrl+S: the window stays usable while it saves (scroll, write, turn pages). The
      tab title and the window title say "saving…"; the dot (●/•) stays until the save is done, then goes.
- [ ] Write a stroke *while* it says "saving…": when the save is done, the dot stays (that stroke is not in the file
      yet). Undo that stroke: the dot goes (the file holds the document as it is now). Redo, Ctrl+S: saved.
- [ ] Ctrl+S twice quickly on a long hybrid PDF: it saves twice at most, one after the other (no error), and the
      last strokes are in the file (close and reopen).
- [ ] Ctrl+S on a big `.xopp` (many pages of ink): the window does not freeze.
- [ ] Close the tab (✕) while it says "saving…": nothing is asked, the tab closes once the save is done. Quit the app
      (window close / Ctrl+Q) while it saves: the window stays until the save is done, then closes (or asks about
      other documents with changes). Reopen: everything is there.
- [ ] Save to a place that cannot be written (a read-only folder, a full SD card): an error says why, the dot stays,
      and closing the tab asks about the unsaved changes.
- [ ] A document with pasted PDF pages: delete a pasted page, Ctrl+S, and right away (while it saves) undo the
      deletion: the page shows its PDF page. Close and reopen: all pages there, with their text (search finds it).
- [ ] Paste PDF pages from another PDF while a document with a large merged PDF saves: the paste may wait a moment,
      then both are in the file after the next save; nothing refers to the cache (close, reopen from another
      folder view or after clearing `~/.cache/xournal-qt/pasted-pages`).
- [ ] Paste a page from another PDF into a long PDF (pgfmanual, a lecture of hundreds of pages) and into the large
      scan: the page is there at once and shows its PDF page; the window does not freeze (it used to for seconds).
      Its thumbnail may be white for a few seconds. Search finds its text a little later. Undo and redo the paste
      right away: fine. Ctrl+S right after the paste: saves once the page is in the merged PDF; close and reopen:
      the page is there with its text.
- [ ] Kill the app (`kill -TERM <pid>`) while it saves a long hybrid PDF: at the next start the document is offered
      for recovery with the latest strokes, and the hybrid PDF is either the old or the new version (it opens).
- [ ] (Fix) Paste a PDF page from another PDF into a document that has a background PDF (best: the large scan):
      the pasted page shows its PDF page on the canvas right away (it used to stay blank for a while), a second
      paste from a third PDF too; the other pages do not flicker or re-render.

## Fuzzy search (qt/fuzzy-search)
- [ ] The library's search field has a "Fuzzy" button, off. Hover (or press and hold on touch): the tooltip is a
      short table of the syntax. Off, the search is as before (`kalman | lqr` finds nothing unless that text is there).
- [ ] Turn it on and type part of a document's name with letters left out (`lctr` for "Lecture"): it is found, first
      of all, with the matched letters orange and underlined on the card. Better matches come first (letters at word
      starts, in a row).
- [ ] `kalman filter` finds documents that have both words anywhere (also on different pages); in the extended search
      (the pages button) a document shows the pages with both words, or all pages with hits when no page has both.
- [ ] `a | b` finds either; `a b | c` is a and (b or c); `(a b) | c` groups; `!draft` drops documents with "draft" in
      the name, the folder or the text; `!archive` drops everything in a folder "Archive".
- [ ] `'word'` finds the whole word only, `^pre` names starting with "pre" (in text: words starting with it),
      `ing$` the ends. A term in a folder's name finds its documents (`uni lect` for Uni/Lecture 3).
- [ ] Type `(kalman` or `kalman |`: a short red hint appears next to the field and the text is searched as plain
      text; complete the expression and the hint goes.
- [ ] Open a result, a page of it or a snippet card: the document opens with the same terms marked (all of `a | b`),
      and the search bar steps through them. Edit the query there: it stays fuzzy; clear it: the next search is
      plain.
- [ ] Close and start the app again: "Fuzzy" is still on. A second window shows it on too; turning it off in one
      turns it off in both.
- [ ] The tab overview (Ctrl+Shift+F) has the same "Fuzzy" button, in the same state as the library's. Open a few
      documents: `lecture !draft` marks the documents that match (title or text), the matched letters of the titles
      are highlighted, a document found by its title alone says "In the name"; with the pages button, a document shows
      the pages on which the whole expression holds. "Names" with Fuzzy on matches the titles fuzzily.
- [ ] A big library (thousands of documents): typing in the fuzzy search stays as responsive as the plain one.

## Reference mode (qt/reference-view)

- [ ] Two canvases side by side (the reference for reading only): writing with the pen on the notes and moving on
      across the divider into the reference: the stroke stays on the notes, the reference does not scroll under it;
      the next stroke on the reference scrolls it. The same with the mouse.
- [ ] On the reference, pen, highlighter, eraser, text and shape tools scroll it like the hand (a tap on a link
      shows the link); nothing is ever written, erased or marked there, and the tab of the reference gets no "●".
- [ ] A two-finger tap on the reference undoes nothing (a two-finger tap on the notes still undoes there).
- [ ] "Open as reference" from the tab strip's menu of another tab (press and hold / right click), from the book icon
      on a card of the tab overview (Ctrl+Shift+E), and from the menu of a card in the library and in Recent: the
      document appears beside the current one (on the left), fitted to its half, with a small book in its tab. An
      untouched new document stays open for the notes. The notes have a thin blue frame.
- [ ] Drag the divider's grip with a finger and with the pen: both halves follow; it stops at a fifth of the width.
      Restart the app: the divider is where it was left.
- [ ] Switch to another tab and back: each tab shows its own reference (or none). The home screen hides it.
- [ ] The reference's pill: the page counter (tap it, type a number, Enter: that page), fit width, swap sides (the
      notes on the left for a left hand; remembered), swap roles (the reference becomes the notes and the other way
      round, nothing is drawn again), × (the split closes, the tab stays). Closing the reference's tab also closes
      the split.
- [ ] Scroll the reference with a finger (fling), the pen, the mouse wheel and the touchpad (with momentum); pinch
      and Ctrl+wheel zoom only the reference; a double tap zooms into a column. The notes do not move meanwhile.
- [ ] PDF text in the reference: with the PDF text tool (even in highlight mode) drag over a line, or press and hold
      a word: the copy button appears in its pill; tap it and paste into the notes (Ctrl+V after a tap on the notes).
      The same with the lasso: select strokes in the reference, copy, paste in the notes.
- [ ] A link in the reference (a table of contents): tap it, "Go to page" goes there in the reference; back with
      Alt+Left after a tap on the reference's pill.
- [ ] Keys: after a tap on the reference (or its pill) Ctrl+C, Ctrl++ / Ctrl+-, Ctrl+0 and Alt+Left act on the
      reference; after a tap on the notes they act on the notes again. Ctrl+Z undoes in the notes (unless the
      reference is written in, see below).
- [ ] Full screen (F11): the split stays, the tool square starts over the notes; with the tool bar docked left, right
      and at the top nothing overlaps.
- [ ] The pen button in the reference's pill: on (highlighted), the reference is written in with the tool in hand
      (pen, highlighter, eraser, text, lasso that moves), its tab gets its "●", Ctrl+Z / Ctrl+Y (and a two-finger
      tap on it) undo there while it was the last one written on; a tap on the notes and Ctrl+Z undoes in the notes.
      A stroke that begins on either side stays on that side across the divider. Off: for reading again. Each tab
      remembers it for its reference; a new reference, and the notes after "swap roles", start for reading. Save
      the reference from its own tab (or when closing it / quitting, which asks).
- [ ] The grid button in the reference's pill: the pages of the reference fill its half (the notes and their page
      sidebar stay as they are); scroll it with a finger and the touchpad; a tap on a page goes there and closes the
      grid; the button closes it too. The page sidebar never shows the reference's pages.
- [ ] With `XQT_PERF=1` and a long PDF as the reference beside the notes: scroll the reference and the notes in
      turn; both show sharp pages quickly, neither waits behind the other's pages rendered in advance. Swap roles:
      no page goes blurry or is drawn again. Switch to a third tab and back: the reference's pages near where it
      was read are still there.
- [ ] (Follow-up) The reference has its own scroll bars (vertical; horizontal when zoomed in): drag them with a
      finger and the pen; the notes keep theirs.
- [ ] PDF text in the reference: press and hold a word (or right click, or the PDF text tool): its two knobs and its
      pill appear on the reference's side and follow it while it scrolls; drag a knob: only the reference's
      selection changes. For reading, the pill offers "Copy text" only; with the pen button on, also highlight
      (with the colours), underline, strike through and paste here.
- [ ] Press and hold (or right click) an empty place of the reference: for reading "Select all", "Go to page…",
      "Fit width" (and "Copy" with a selection); with the pen button on the same pill as on the notes (Paste,
      Select all, Image…), acting on the reference.
- [ ] Lasso or rectangle in the reference: its bar appears above the reference's pill with Copy (and Deselect);
      with the pen button on, also Cut, Paste and Delete.
- [ ] Markdown in the reference: with the pen button on, tap a Markdown text box of the reference with the text tool:
      the panel beside the pages edits the reference's text (its tab gets the "●"); for reading, the text tool only
      scrolls there and Ctrl+M (Markdown) still writes in the notes.
- [ ] Ctrl+S after writing in the reference (pen button on, last tap on the reference): the reference is saved (its
      "●" goes), the notes keep theirs. A new, never saved reference: its tab comes forward and asks where to save.
      For reading (pen button off), Ctrl+S saves the notes as before.
- [ ] (Fix) Fit width in a narrow half beside a reference (and in a narrow window): the page fills the width and
      there is no horizontal scroll bar on either side (it used to appear for a few pixels).

## Presentation and horizontal scrolling (qt/present)
- [ ] Page number jump: with a document open, type `12` on the keyboard (no text being written): "Go to page: 12
      of N" appears over the page; Enter goes to page 12 (a number past the end goes to the last page), Escape or a
      tap on the page cancels, Backspace takes a digit back, the number pad works too. Alt+Left goes back to where
      you were. Typing digits into a text on the page, a Markdown box, the search field or a dialog does not open it.
- [ ] 16:9 pages: New document → Paper "16:9 (presentation)": it switches to Landscape by itself; the pages are
      wide slides (PowerPoint's 13.33 × 7.5 in). Insert pages… after an A4 page with "16:9 (presentation)": the new
      pages are slides, the A4 page stays. Save and open the `.xopp` in upstream Xournal++: the same page sizes.
- [ ] Fit width follows the page in view: in an A4 document, paste or insert a 16:9 page; on an A4 page, tap the
      zoom button (fit width): the A4 page fills the width (not smaller because of the slide). Scroll to the slide
      and tap it again: the slide fills the width. Scrolling back keeps the zoom (fit width is not re-applied by
      itself). With two pages side by side the row in view fills the width. Double tap on a zoomed-in page: back to
      that page's width.
- [ ] Scroll sideways: press and hold (or right click) the layout button in the page / zoom pill → "Scroll
      sideways". The pages stand side by side, each as high as the window; the pill shows ‹ 3 / 40 ›. ‹ › go to the
      previous / next page with a short slide, and so do ← → and Page Up / Down (Home / End: first / last page);
      tapping › several times quickly goes that many pages on smoothly.
- [ ] With "Stop on whole pages" (on by default): a finger swipe goes to the next or previous page; a slow drag of
      less than half a page springs back, more than half goes on; a strong fling goes on several pages and still
      stops on one. Two fingers on the touchpad (sideways or up / down) do the same; the mouse wheel goes one page
      per notch. The pages are sharp when they arrive (the next and previous pages are drawn in advance): watch
      for blurry pages while paging quickly through a PDF.
- [ ] Turn "Stop on whole pages" off: swiping, the touchpad and the wheel (up / down scrolls sideways) scroll freely
      with momentum, as the pages going down do.
- [ ] Layout menu → Rows +: two rows of pages (pages 1 and 2 above each other, then 3 and 4, …), both rows fit the
      height; "Two pages side by side" and "Book" keep the pairs side by side. Resize the window: the pages fit the
      height again, on the same page. Pinch or Ctrl+wheel zoom: the zoom stays when the window is resized.
- [ ] Close and start the app again: the sideways layout, rows and "Stop on whole pages" are kept.
      Switch back ("Scroll sideways" off): pages go down as before, on the same page.
- [ ] Present from the tool bar (the presentation button next to full screen), from ⋮ → "Present (F5)", or with
      F5: the window goes full screen on black, the current page fills the screen (a 16:9 slide edge to edge on a
      16:9 screen, an A4 page in the middle with black on both sides), no pill, no scroll bars; the tool square stays
      and "12 / 40" shows at the bottom for a moment after each page change, then fades.
- [ ] Keys like PowerPoint: Space, →, ↓, Page Down next; ←, ↑, Page Up, Backspace previous; Home / End; a number
      and Enter goes to that page. A presenter remote (clicker, sending Page Up / Down) pages too.
- [ ] Swipe with a finger: one page per swipe, even a strong one; a short slow drag springs back. Two fingers on
      the touchpad page as well.
- [ ] Write with the pen while presenting: the stroke stays on the page, nothing pages or moves. Tap the tool
      square: tools and colors; "Stop presenting" ends it there. Pinch to zoom into a slide: it scrolls within the
      slide; a double tap brings it back to the whole slide.
- [ ] Escape ends presenting: full-screen editing, with the zoom and layout from before, on the page that was
      presented. A second Escape leaves full screen. From full screen, the tool square → "Present" starts it
      again; F11 there leaves full screen and presenting at once.
- [ ] A document with pages of different sizes (A4 and 16:9): each fills the screen when it is shown.
- [ ] Performance: page quickly through a long PDF while presenting: each page arrives sharp (drawn in advance),
      no grey or blurry page on arrival.
- [ ] With a reference beside the notes: tap the reference, type `4` Enter: the reference goes to page 4, the
      notes stay. Scrolling sideways, ← → page the side that was tapped last. The reference pill's fit width fits
      the reference's current page. Present (F5): the notes alone fill the screen; after Escape, Escape the
      reference is back beside them.
- [ ] Full screen (editing) with several documents open: a slim bar at the top centre shows one dot per document
      (the current one filled, a small orange mark on unsaved ones). Tap it: all open documents. Swipe left / right
      along the bar with a finger: the next / previous document, its title shows for a moment. Swiping on the page
      never switches documents. With one document, while presenting or with the search bar open, the bar is
      hidden; it does not cover the tool square or the page / zoom pill, and spans a reference split in the middle.
      With more than 12 documents it shows "3 / 17".

## Fuzzy search in text (qt/fuzzy-text)
- [ ] Open a PDF with the word "turbine" in its text, search with Fuzzy on (from the library or the tab overview):
      `tbine` finds it, `turbnie` (letters swapped), `trbine` (one left out) and `turbime` (one wrong) too; the whole
      word "turbine" is marked on the canvas, in the sidebar and in the page grid, and the count matches the marks.
- [ ] Short terms stay strict: `tb` finds only what contains "tb" (as before); `tbn` does not mark "turbine".
- [ ] In the library with Fuzzy on, `tbine` lists the documents with "turbine" in their text. Search `turbine`:
      documents with "turbine" or "turbines" come before those that only have a typo like "turbnie". The card's snippet shows "turbine", the pages button shows the page, and its
      picture marks the whole word "turbine" (also `^turb` marks only word starts now, `'turbine'` whole words).
- [ ] A Markdown file with "turbine" in a passage: `tbine` shows its snippet card with "turbine" marked; opening it
      marks the same word in the document.
- [ ] A big library: turning Fuzzy on, the first fuzzy search is not noticeably slower than the next ones (the words
      are prepared in the background); typing stays as responsive as the plain search.
- [ ] Settings → Search (a new tab after Documents): the "Fuzzy search" switch is in the same state as the "Fuzzy"
      button of the library's search field; switch it there, the button follows, and the other way round.
- [ ] Typo tolerance "Off": `turbnie` no longer finds "turbine" (`tbine` still does). "Up to 2 letters":
      `trasnfromation` finds "transformation"; back to "1 letter" (the default) it does not.
- [ ] Hover the "Fuzzy" button: a one-line tooltip that mentions the help. Long press it (touch) or right-click it
      (mouse), in the library and in the tab overview: the fuzzy search's help opens (the button does not toggle),
      scrolls with a finger, and closes with a tap outside or Escape. Its typo sentence follows the setting in
      Settings → Search, which has a "Help" button that opens the same help.

## Hybrid PDF flow and Share (qt/hybrid-flow)
- [ ] ⋮ → Save as… (and Ctrl+Shift+S) on a new document: one dialog with the file types "Xournal notes (.xopp)"
      (chosen) and "PDF with notes, editable (.pdf)". Switch the type: the name's extension follows (in KDE's
      dialog too). Save as PDF: the tab title is the `.pdf`, Ctrl+S writes it again, and Save as… now starts on the
      PDF type with its own name. Type `x.pdf` with the .xopp type chosen: it is saved as a PDF (the typed extension
      wins). There is no "Save as hybrid PDF…" in ⋮ any more; "Export as plain PDF…" (Ctrl+E) still flattens.
- [ ] A new document saved as `notes.xopp`, then Save as… → "PDF with notes": a question "This document was saved
      as notes.xopp" with "Move notes.xopp to the trash" (chosen), "Keep it updated for Xournal++", "Keep it as it
      is" and "Don't ask again". Cancel: nothing is written. Save: `notes.pdf` is written, `notes.xopp` is in the
      desktop trash (restore it from there), a note says so, and the library shows one card `notes`.
- [ ] `lecture.pdf` with notes saved as `lecture.xopp`, then Save as… PDF (`lecture.notes.pdf`) with "Keep it
      updated for Xournal++": `lecture.xopp` opens in Xournal++ with the notes; write more, Ctrl+S, reopen it in
      Xournal++: the new ink is there. Close and reopen `lecture.notes.pdf` in xournal-qt, write, Ctrl+S: still
      updated. Delete `lecture.xopp`, save again: it does not come back. `lecture.pdf` itself never changes.
- [ ] With pasted PDF pages in a `.xopp` (it has a hidden `.name.pages.pdf`), save it as a PDF with the trash
      choice: the pages keep showing, later saves work, and both files are in the trash.
- [ ] "Keep it as it is" next to the PDF of the same name (`notes.xopp` + `notes.pdf`): the library shows one card
      "notes" (PDF) that opens the PDF with notes. Edit `notes.xopp` in Xournal++ and save it: after a refresh the
      library shows two cards "notes", the PDF and the `.xopp`, each opening its own file.
- [ ] "Keep it as it is" with "Don't ask again": the next document is not asked, its `.xopp` stays. Settings →
      Documents → Hybrid PDF shows "Keep it as it is"; set it to "Ask each time": asked again.
- [ ] ⋮ → Share… on a PDF with notes that has unsaved changes → "PDF with notes": it is saved, then Dolphin opens
      with the file selected. Drag it into an email or chat; the other side sees the notes.
- [ ] Share… → "Copy the PDF with notes": a note "PDF copied". Paste into Dolphin (a copy of the file appears),
      into Telegram / a browser upload field / a chat app: the PDF is attached. Paste into a text editor: its path.
- [ ] Share… on a `.xopp` document → "PDF with notes": asked "Save as PDF with notes…" or "Save a PDF copy…".
      The copy leaves the tab as the `.xopp` (title, unsaved changes); Save as goes through the .xopp question.
      "Copy the PDF with notes" on a `.xopp` does not ask: the clipboard gets a PDF copy from the app cache.
- [ ] Share… → "For Xournal++": a folder dialog. Choosing the document's own folder: refused with a message.
      Another folder: `name.xopp` and `name.xopp.bg.pdf` appear there, Dolphin shows both selected, the note offers
      "Copy". Open that `.xopp` in upstream Xournal++ (also after moving both files elsewhere): all pages with their
      PDF pages and the notes, editable. A second export into the same folder is "name (2).xopp".
- [ ] The tab's context menu → Share…: that tab comes to the front with the Share choices. A PDF card in the library
      (or Recent) → Share…: the PDF itself (shown / copied), and "For Xournal++" exports without opening a tab. A
      notes card (or a PDF with its `.xopp`) → Share…: it opens, then the Share choices for it.

## Markdown and text editor (qt/md-editor)
- [ ] Open a `.md` from the library: no read-only note, no ink tools in the tool bar. Tap (pen, mouse or finger)
      anywhere on a page: the cursor is there, the block under it shows its Markdown, the others stay formatted.
      Type, press Enter in a list (the next item), Backspace right after an empty `- ` (the mark goes at once).
      A drag with the pen or the mouse selects; a finger drag scrolls.
- [ ] Type on page 3 of a long file until a page is added; delete until it goes again. Undo (Ctrl+Z and the undo
      button) goes back word by word, not the whole edit at once.
- [ ] The tab shows the unsaved dot; typing back to the saved text removes it. Ctrl+S: no dialog, the dot goes, the
      library card shows the new text. Open the file in another editor: only what you typed changed (a Windows
      file keeps `\r\n`, a file without a newline at its end stays so; `git diff` shows only your lines).
- [ ] Close the tab with unsaved changes: the usual question; "Save" writes the `.md`, no `.xopp` appears.
- [ ] Change the open file in another editor and save there, with no changes in the app: the tab shows the new
      text within a second (a note says so), the cursor stays. With changes in the app: a dialog "Changed in
      another app": Reload shows the other version (Undo brings yours back), Keep mine keeps yours (saving writes
      over it).
- [ ] Kill the app (`kill -9`) with an unsaved `.md`: at the next start the recovery offers it by its name;
      recovered, the tab is the `.md` with your text, modified; the file on disk is unchanged until you save.
- [ ] A `.md` that is not UTF-8 (e.g. Latin-1) or read-only on disk opens read-only with a note saying why.
- [ ] Open a `.txt` (the library's "Show" → Text and code, or Open): monospaced lines as they are, `**` and `#`
      shown as typed, no highlighting. Type, Enter (the new line keeps the indentation), Tab (a tab), Ctrl+B does
      nothing. A long `.txt` flows over pages; Ctrl+S writes it back (line ends as they were).
- [ ] Open a `.py` or `.tex` (Show → Text and code): read-only with highlighting, the note has "Edit anyway" (and ⋮
      too). Tap it: a warning that the app does not know the format; Cancel keeps it read-only, OK shows it as
      plain text to edit, in the same tab. Close and open it again: editable at once, no warning. Another file
      asks again.
- [ ] "Open externally" (the arrow-out-of-a-box button in the tool bar, ⋮, and the library card menu of a `.md`,
      `.txt`, code file or image): the file opens in its system app (e.g. Kate / a code editor). With unsaved
      changes in a `.md`: asked "Save before opening it elsewhere?"; Save and open saves, then opens. Change and
      save it in the other app, come back to the window: the tab shows the new text. A `.xopp` or PDF has no such
      button or entry.
- [ ] With a `.md` open, press and hold (or right-click) the layout button: "Text on pages" is checked. Choose "Text
      on one continuous page": one long page, no page breaks; typing new paragraphs makes it longer. Open another
      `.md`: continuous as well. Back to "Text on pages": A4 pages again, the text unchanged, no unsaved dot.
      Watch typing speed in a long file on the continuous page (it lays out the whole text per key).
- [ ] In a `.md`, "Edit as notes" (the notebook-with-pen button, or ⋮): a new tab "name.xopp" next to it with the
      same pages; write on them with the pen, and the text tool edits the Markdown text there. Close it: asked to
      save; Save suggests `name.xopp` next to the `.md`. The `.md` is unchanged, and the library shows two cards
      (MD and the notes).
- [ ] Library: the new button (file with a plus) opens a menu: New document… (as before), New Markdown file…, New
      text file…. "New Markdown file…", type a name, Enter: `name.md` appears in the current folder, opens, and you
      can type at once. The same name again gives "name (2).md".
- [ ] Share… on an open `.md` (⋮ or the tab menu) and on a `.md` / `.txt` card: only "The file itself" and "Copy the
      file" (no PDF with notes, no Xournal++ copy); with unsaved changes it saves first. ⋮ of a `.md` has no "Save
      as…".

## Android APK (qt/android-apk)
Build and install as in [../android.md](../android.md) (`qt/scripts/android-build.sh`, then
`adb install -r build-android/android-build/build/outputs/apk/debug/android-build-debug.apk`). With a mouse and a
keyboard on the phone.
- [ ] The APK installs (adb or opening the file); "Xournal Qt" with the app icon is in the launcher.
- [ ] It starts without crashing and shows the library "Default" (empty the first time). The first start may take a
      while (fontconfig scans the system fonts); the second start is quicker.
- [ ] New document → Create: a page appears, the document is in the library, and it is still there after closing and
      starting the app again.
- [ ] Draw with the mouse: strokes appear, undo/redo work. The pen colours and widths change the strokes.
- [ ] Text tool: a text box shows real letters (no boxes), also ä ö ü ß; the page thumbnail shows the same text.
- [ ] A PDF pushed into the library folder (`adb push file.pdf /storage/emulated/0/Android/data/org.xournalqt.app/files/Documents/Xournal_Libraries/Default/`)
      appears in the library and opens; its pages render, text included.
- [ ] Settings open; the tool bar icons are drawn (not empty squares).
- [ ] Fold and unfold the phone, and put the app in split screen: it keeps running, the page stays visible.
- [ ] `adb logcat --pid=$(adb shell pidof org.xournalqt.app)` shows no crash (a crash: note the backtrace).

## Reference pop out (qt/reference-popout)
Three documents open: A (notes), B, C, in this order; in A, "Open as reference" on C.
- [ ] The reference pill has a new button (a window with tabs), tooltip "Show as a tab". Tap it: the split closes, C
      fills the window, and the tab strip reads A, C, B (C moved right after A).
- [ ] Ctrl+Shift+Tab: A, without the split (the pair is not kept; "Open as reference" on C shows it again). Ctrl+Tab:
      C again. The arrows beside the tab overview do the same.
- [ ] Reference already beside the notes (A, C as neighbours, either side): "Show as a tab" moves no tab, only shows C.
- [ ] In full screen: split with a reference, tap "Show as a tab": still full screen, the reference fills it, the tab
      dots at the top mark C; a swipe along the dots goes back to A.
- [ ] An undocked window with its own tabs: the pop out stays within that window.

## Links between documents (qt/links)
Make a folder with a lecture (`Lectures/kalman.xopp` with a chapter "Prediction step" on page 4, "Start a chapter
here…" in the page menu) and a note `Notes/a.md` with `[Kalman](../Lectures/kalman.xopp#chapter=Prediction%20step&page=2)`
and `[[b#Part two]]` (a `Notes/b.md` with a heading "## Part two" a few pages down).
- [ ] In `a.md`, tap the link with a finger (or Ctrl+click with the mouse or pen): a popup with "kalman.xopp, chapter
      "Prediction step"" and Open in a new tab / Open as reference / Open here / Remember my choice. The pen with
      the pen tool does not open it (in a `.xopp`'s Markdown box it keeps writing).
- [ ] "Open in a new tab": the lecture opens at page 4 (the chapter, not page 2). Alt+Left (or ← in the pill) goes
      back to `a.md`, Alt+Right forward to the lecture. The same link again switches to the open tab.
- [ ] "Open as reference": the lecture beside the note, at page 4.
- [ ] "Open here" with "Remember my choice": the note's tab goes, the lecture is in its place; Back brings the note
      back. The next tap opens at once, no popup. Settings → Documents → Links → "Ask each time" brings the popup
      back.
- [ ] Rename the chapter in the lecture, tap the link again: page 2 opens with the note "Chapter "Prediction step"
      not found, opened page 2".
- [ ] `[[b#Part two]]` opens `b.md` at the page with "Part two".
- [ ] In the lecture, the page sidebar's page menu → "Copy a link to this page", open a note and press Ctrl+V on its
      page (nothing selected): a small blue "🔗 kalman, page 3" appears where the view is; a tap on it (hand tool
      or finger) offers to open the lecture there. Undo removes it. Open the note in Xournal++: it shows
      `[🔗 kalman, page 3](../Lectures/kalman.xopp#page=3…)` as text.
- [ ] Select a sketch (lasso), Ctrl+V the link: the marker sits at the sketch's top right.
- [ ] Write Markdown on a page (or in a `.md`), Ctrl+V the link: `[kalman, page 3](../Lectures/kalman.xopp#page=3)`,
      relative to that document. The same in the editor beside the page.
- [ ] Contents sidebar: press and hold a chapter → "Copy link to this chapter"; ⋮ → "Copy link to this page"; a
      library card's menu → "Copy link"; a page with hits in the extended library search, press and hold → "Copy
      link to this page". Paste each into a text editor of another app: a Markdown link with the full path.
- [ ] Open the lecture, ⋮ → "Linked from…": `a.md` and the note with the marker are listed; a tap opens one.
- [ ] Close everything but the library. Rename the lecture in the library ("Kalman filter"): a note "Updated N links"
      comes; `a.md` (opened in the app or in a text editor) now links to `../Lectures/Kalman%20filter.xopp#…` and
      nothing else in it changed (`git diff` or `diff` against a copy: only those links). The marker in the note
      follows too (open it in Xournal++: the new path). Move `a.md` into a subfolder: its own links become
      `../../Lectures/…`.
- [ ] With `a.md` open and unchanged, rename the lecture again: the link changes in the open tab, it is saved, and
      Ctrl+Z brings the old link back.
- [ ] Move the lecture into another folder with the file manager, then tap the link in `a.md`: the lecture opens
      from its new folder and a dialog offers "Update the link"; Yes changes the link in `a.md`.
- [ ] A link to a file that is nowhere (`[x](missing.xopp)`): "Document not found … Locate it?" → Open → pick a
      file: the link now points there and it opens.
- [ ] In a note with a link marker to the lecture (and a Markdown box with `[web](https://example.org)`), Save as
      → "PDF with notes": open the PDF in Okular or Firefox: the marker's text is a link; clicking it opens
      `lecture.pdf` at the linked page (Okular may ask first), the web link opens the browser. Open the PDF in the app
      again: no "changed in another app" question; save again: the links are still there once each.

## Windows build (qt/windows-build)
The zip from the GitHub Actions run "xournal-qt Windows" (artifact `xournal-qt-windows-x64`), unpacked anywhere, on
the Surface or another Windows 10/11 machine. See [../windows.md](../windows.md).
- [ ] `bin\xournal-qt.exe` starts (no console window) and shows the library "Default" in Documents\Xournal_Libraries.
      SmartScreen may warn first (unsigned): "More info" → "Run anyway".
- [ ] The tool bar icons are drawn (not empty squares); the Material style is used.
- [ ] New document → Create: draw with the mouse and with the pen; undo/redo; the document is saved in the library and
      is still there after a restart.
- [ ] Text tool: real letters, also ä ö ü ß; the page thumbnail shows the same text.
- [ ] A PDF in the library (Explorer: copy it into the folder): it appears, opens, its text renders; annotate, save,
      reopen.
- [ ] A folder and a document whose names have umlauts (e.g. `Übungen\Prüfung.xopp`): both open and save.
- [ ] Open a second window of the same library from Explorer (`bin\xournal-qt.exe <folder> <file>`): the file opens as
      a tab of the running window.
- [ ] Card menu → "Show in file manager": Explorer opens with the file selected. "Open with the system app" on a
      `.docx` or image opens the right program.
- [ ] Print: the Windows print dialog; a real printer and "Microsoft Print to PDF" both get the pages (upright,
      landscape pages turned onto the sheet), the page range and copies are honoured.
- [ ] Settings stay after a restart (`%LOCALAPPDATA%\xournal-qt`); caches are in `%LOCALAPPDATA%\cache\xournal-qt`.
- [ ] Kill the program in the Task Manager with an unsaved document: at the next start it offers to recover it.
- [ ] `bin\xournal-qt-cli.exe some.xopp --create-pdf out.pdf` in a terminal writes the PDF.

## Archive export (qt/archive-export)

- [ ] Take a lecture PDF with notes (pen with pressure, a highlighter over its text, a text box, a Markdown link to
      another PDF, an inserted ruled page). ⋮ → "Export for the archive…": the dialog explains what an archive PDF is
      and offers "Next to the document, as lecture.archive.pdf" or a folder. Export (with an unsaved stroke): the
      window stays usable, then "lecture.archive.pdf is a PDF/A-3b file"; "Show in folder" shows it selected. The tab
      still has its unsaved change (the dot). Open `lecture.archive.pdf` in Okular,
      Firefox and Acrobat: the ink looks as in the app, the highlighter lets the text show through, and the ink can
      not be hidden (Okular: "Show annotations" off changes nothing; Acrobat's comment list has only the link). The
      link opens the other PDF.
- [ ] Acrobat (Reader is enough): the file opens with the blue "PDF/A" bar ("This file claims compliance with the
      PDF/A standard"); Attachments shows `document.xopp`. File → Properties: the title is the lecture's.
- [ ] Open `lecture.archive.pdf` in xournal-qt: every stroke is editable, and none is shown twice (erase one: nothing
      stays behind). Ctrl+S, open it in Okular again: the stroke is gone there too, and Acrobat still says PDF/A.
- [ ] Share… (⋮, the tab menu, a PDF card in the library) → "For the archive (PDF/A)" opens the same dialog. From a
      card, choose "In a folder I choose…": the archive appears there without a tab opening.
- [ ] Export a PDF whose fonts are not embedded (`pdffonts file.pdf` shows "no" under "emb"; old PDFs with
      Helvetica or Times often are): the report says "not PDF/A" with the font names, and the file still opens
      everywhere; Acrobat shows no PDF/A bar for it.
- [ ] Library menu → "Export library as archive…" on a real library (with subfolders, a lecture PDF with its
      `.xopp`, a hybrid PDF, `.md` notes, images, a Word file): choose a folder outside the library (a folder inside
      it is refused with a message). A progress dialog counts the files; the window stays usable. At the end the
      summary; "Show in file manager" shows the new folder "<library> archive <date>" with the same subfolders, a
      PDF per document, the other files as they were, and README.txt. The library is unchanged (no new files there).
- [ ] In the archive folder, open a note that links to a lecture (in Okular or Acrobat): the link opens the lecture's
      archive PDF at the right page.
- [ ] Start it on a big library and press Cancel: it stops after the current file, the summary says "cancelled", and
      the README says the archive is incomplete.
- [ ] With a folder open in the library, "Only this folder" archives that folder and its subfolders only.
