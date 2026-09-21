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
- [ ] Zoomed in, the page never draws over the sidebar.
- [ ] Page counter and zoom (−, %, +) in the pill at the bottom right; tapping the % fits the page width.
- [ ] Portrait (narrow window): the tool bar can be swiped sideways to reach all tools.

## Settings and tab overview (M5c)
- [ ] Gear button: the settings sheet opens; every section scrolls with a finger; sliders and switches work with pen and finger.
- [ ] Changing the pressure multiplier or the stabilizer changes the next stroke without restarting.
- [ ] Settings → Pen: "Lower side button" / "Upper side button" set to "Hand": that button scrolls instead of erasing. Set back to "Eraser".
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
- [ ] Delete removes the selected pages; the note at the bottom offers Undo; Ctrl+Z in the sidebar/grid undoes page changes only, Ctrl+Z after drawing undoes ink.
- [ ] Press and hold a page, drag it (or the selection) to another place in the sidebar or the grid: a bar shows the drop place, the view scrolls at the edges.
- [ ] Page grid on touch: "Select", tap pages, then Copy / Cut / Paste / Duplicate / Delete in the bar.
- [ ] Two-finger scrolling on the touchpad in the grid, sidebar and tab overview continues after lifting the fingers.

## Library and home screen
- [ ] `xournal-qt` without arguments: the home screen shows the library "Default" (folder created in Documents/Xournal_Libraries); `xournal-qt ~/some/folder` shows that folder as library, in its own window.
- [ ] Drag PDFs and .xopp files from Dolphin onto the library: they are copied (a note says how many); dropped on a folder card they go into that folder.
- [ ] Previews appear (first page, with annotations); "PDF ✎" marks annotated PDFs; a lone PDF shows up and opens for annotating; saving it creates the .xopp next to it and the card stays one item.
- [ ] New document: name, background, paper, landscape: it opens and the file exists in the current folder.
- [ ] Rename a document with a PDF: both files are renamed and the document still opens with its PDF (also when it is open in a tab; the tab title follows).
- [ ] New folder, enter it, breadcrumbs back; press and hold a card and drag it onto a folder or a breadcrumb: it moves there.
- [ ] Select several cards (circle, Ctrl/Shift+click, or "Select" in the menu then taps): Open opens them all as tabs; Copy to… / Move to… with the folder dialog; Trash.
- [ ] Library search: folders with the name (tap: opens the folder), documents with the text or the name, hit count and snippet; tapping a document opens it at the first hit.
- [ ] Import → "Import a folder with its subfolders…" (and dropping a folder): the whole folder tree appears in the library with its PDFs and .xopp files.
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
- [ ] F1 (or Ctrl+/) lists all shortcuts by group; Esc closes it.
- [ ] "Change…" opens the settings at "Shortcuts": tapping a row asks for keys; pressing e.g. Ctrl+Alt+P for "Add a page" makes that key work at once and Ctrl+N stop working.
- [ ] Ctrl+Shift+N (new document), Ctrl+Shift+E (all documents), Ctrl+Shift+L (library), Ctrl+Shift+S (save as) and Ctrl+Tab work - also with a second window open.
- [ ] A key another action already uses is marked in red with its name; Backspace in the dialog removes a shortcut; "Default keys" puts everything back.
- [ ] The chosen keys are still there after a restart.

## Tool bar, full screen, colors, pages
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

## Table of contents
- [ ] A PDF with bookmarks (e.g. a book or thesis): sidebar "Contents": indented headings, current section highlighted, tap goes there, arrows collapse.
- [ ] Contents overview (toolbar button or Ctrl+Alt+O): chapters big, sections smaller and indented, the pages of each section in a row (swipe sideways); tap a page: it opens there; "Chapters only"; − / +.
- [ ] The grid button next to the page number in the pill opens the page grid.

## Package
- [ ] `sudo apt install ./build-deb/packages/xournal-qt_0.1.0_amd64.deb`: Xournal Qt in the application menu with its icon; PDFs and .xopp files offer "Open with Xournal Qt"; right click on a folder in Dolphin: "Open as Xournal Qt library" opens it in its own window.

Report problems with the input log (see qt/spikes/inkpad/README.md) or a screen recording.
