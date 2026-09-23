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

## Per-folder library cache (qt/library-index)
Try these on a **copy** of a library, or on a new one. Opening the real OneDrive library with this build changes its
cache on disk (and, once the whole block is in, converts the old one), which OneDrive then syncs.
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
      `.xournal_library/index/`, `previews/`, `pages.json`) and open the copy. **Opening the real OneDrive library
      converts it for good** (the old files are removed and OneDrive syncs that). After a few seconds: `index/`,
      `previews/` and `pages.json` are gone from the root's `.xournal_library/`, each folder has its packs, no
      progress of indexing was shown (nothing read again), the search finds text at once, the cards show their
      previews without drawing them again, and the cards still show when and at which page documents were read.
      Note how long the conversion took for a big library (the packs appear one folder after the other).
