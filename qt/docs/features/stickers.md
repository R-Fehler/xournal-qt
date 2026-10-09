# Stickers: reusable content per library

The author: "a library could store a set of copy-pastable items that work as stickers or templates,
similar to GoodNotes stickers … select things (all kinds of items on the page, including ink, optionally with a pixel
screenshot of the PDF canvas background), and save that into the template/sticker library. this would be stored in
the library directory but we expose an option to export/copy them to other libraries of xournal-qt. per default they
are also directly in the clipboard. when a user comes back … they click the sticker tool, a grid preview window
(sortable by last used or a fixed user defined order, could have subdirs as well for different lectures/topics) opens
up with the renderings of these stickers and once we select a sticker it's being copied into the clipboard and also
directly pasted into the current page and selected so the user can move and scale it … saved or stored as xopp files
so they can be shared and exported/imported and viewed."

Decided with the author: a visible `Stickers/` folder per library (a fixed English name, with
subfolders), an app-wide set in the app's data folder, the own order in a hidden file inside each sticker folder (so
it syncs), pasted at the original size (made smaller only to fit the page), selected after pasting, also copied to the
clipboard when saved. A pasted sticker is a group ([groups.md](groups.md)): a tap on any part selects all of it.

## Where stickers are

- **The library's set:** the folder `Stickers/` at the root of the library, visible like any other folder (a file
  manager, a sync app and Xournal++ see it). Its subfolders are topics ("Lecture 3", "Chemistry"); one level or more.
  The name is the same in every language, so a library synced between devices in other languages finds it.
- **The app-wide set** ("In all libraries"): `<AppDataLocation>/stickers/` (on Linux
  `~/.local/share/xournal-qt/stickers/`), with subfolders the same way. It is there in every library.
- **The built-in set** ("Built in"): the collections that come with the app, read-only, in the app's resources
  (`<resources>/stickers/<collection>/`; see [Built in](#built-in)).
- **A sticker** is one `.xopp` file (its name is the sticker's name), or a picture (`.png`, `.jpg`, `.jpeg`,
  `.webp`): a picture dropped into the folder is a sticker too, pasted as an image.
- **Own order:** a hidden file `.sticker-order.json` in each sticker folder (`{"order": ["Arrow.xopp", …]}`): it
  syncs with the folder. Stickers not in it come after the ordered ones, by name.
- **Last used:** per library, in its config folder (`Library::configDir()/stickers.json`, not synced: it is how this
  device was used), by the sticker's path.

## A sticker file

A normal `.xopp` that Xournal++ opens (upstream's LoadHandler reads it without a warning):

- **one page**, as large as the content plus 6 points around it (`stickers::MARGIN`); the paper is plain, of the
  colour of the page the content came from (no ruling, no PDF);
- the content moved so its top left is at (6, 6): ink, shapes, images and LaTeX in `Layer 1`; Markdown text boxes in
  the page's `Markdown` layer (at the bottom, as the app keeps it); whole sticky notes as their layers on top;
- with "With the PDF behind it": a picture of the background behind the content (the PDF page, a background image,
  the paper and its ruling; never the ink: `region::Request::layers = false` of the snip's renderer) as an image in
  a layer of its own at the bottom, `Sticker picture`. At least 200 dpi (`region::MIN_DPI`), at most about 4
  megapixels (`region::MAX_PIXELS`). A selection made with the lasso cuts the picture to the lasso's shape
  (transparent outside it); a rectangle, a tap or a selection moved since: the rectangle around the content;
- written with the app's `PictureSaveHandler`, so the pictures of its Markdown boxes travel inside it
  ([md-images.md](md-images.md)), with a preview of its page in the file (as every `.xopp` the app writes).

## Saving a sticker

- **Where:** "Save as sticker…" in the selection's pill (an element selection, several notes with elements) and in a
  selected sticky note's pill; also the "+ Save selection" button of the sticker picker while something is selected.
- **The dialog:** the name (the first words of the first text selected, else "Sticker <date>"), the folder (the set's
  root or one of its subfolders, or a new one), "With the PDF behind it" (offered when the page shows a PDF page or a
  picture), "In all libraries" (the app-wide set instead of the library's).
- A name that is taken becomes "name (2)". The file is written off the UI thread (the picture too); "Saved sticker
  “name”" says it worked.
- **It is on the clipboard too** (as the author wanted): the sticker as the app copies a selection of notes and
  elements (`sticky::GROUP_CLIPBOARD_MIME`), with its picture for other apps. Ctrl+V pastes it again at once.
- Saving works in a document opened for reading only too (nothing in it changes). Not in a Markdown or text
  document (no selection of elements there).

## Using a sticker

- **The sticker button** (an item of the top bar, next to the image button, or wherever the user put it; in the
  phone's "All tools" sheet under "Insert") opens the **picker**: a popup beside the button, a bottom sheet on a phone.
- **The picker:** "This library" / "All libraries" (the app-wide set) / "Built in", the folders of that set as chips
  ("All", then each folder), a search (the name and the folder), the order ("Last used", "Own order", "Name", "Date
  added"), and the grid of previews (three columns on a phone) with the names under them. "+ Save selection" while something is
  selected. An empty set says how to make a sticker.
- **A tap on a sticker** loads it off the UI thread, puts it on the clipboard and pastes it on the current page: at
  its original size, in the middle of the visible part of the page, made smaller only when it is larger than the page;
  selected (to move and scale it right away); one undo step ("Paste sticker"). A Markdown box of the sticker goes
  into the page's Markdown layer, a sticky note on top of the page's layers, the rest into the selected layer. Not in
  a document opened for reading only (the button is not offered there).
- **A picture sticker** is pasted as an image at the size its resolution says (else a point a pixel), made smaller to
  fit the visible part of the page as pasted pictures are.
- **A Markdown or text document** has no sticker button. A sticker used elsewhere is on the clipboard, and Ctrl+V
  in Markdown pastes its picture the Markdown way ([md-images.md](md-images.md)).

## Built in

The author: "allow the user to remove these groups if they are not using IEC for example anywhere in their life, with
a restore feature". Only standardized drawings that are slow or hard to draw well by hand come with the app (no
arrows, callouts, boxes, Venn diagrams: the pen, the shape tool and the user's own stickers cover those):

| Collection (id) | What |
| --- | --- |
| Circuit symbols (IEC) (`circuits-iec`) | 26 symbols in IEC 60617 style: resistor, variable resistor, potentiometer, capacitor, polarised capacitor, inductor, diode, Zener diode, LED, photodiode, NPN and PNP transistor, cell, battery, DC voltage, AC and current source, switch (open), push button, lamp, ammeter, voltmeter, ground, op-amp (IEC box and triangle), fuse. Leads are short straight wire ends, so they meet drawn wires |
| Logic gates (`logic-gates`) | AND, OR, NOT, NAND, NOR, XOR, XNOR in the distinctive (ANSI/IEEE) shapes |
| Logic gates (IEC) (`logic-gates-iec`) | the same seven as IEC rectangles (&, ≥1, 1, =1, with negation circles) |
| 3D solids (`solids`) | cube, cuboid, cylinder, cone, sphere, square pyramid, triangular prism, tetrahedron in cabinet projection (depth at 45°, halved), hidden edges dashed |
| Lab glassware (`lab`) | beaker, Erlenmeyer flask, round-bottom flask, test tube, burette, pipette, measuring cylinder, Bunsen burner, condenser, funnel, benzene (Kekulé and circle) |

- **The files** are made by `qt/resources/stickers/generate.py` (Python's standard library, reproducible; run it
  after changing a drawing and commit what it writes) as the app writes a sticker file (above), black lines of 1.2 pt
  (labels 1.0 pt, as strokes: a sticker is one group that scales cleanly), a circuit symbol about 15–20 mm long, a
  solid 25–35 mm. Each collection's `names.json` names it and every sticker in English and German (the first name of
  each language is shown, the others are searched too); `collections.json` gives the collections' order and a hash.
  Our own work, CC0 (`LICENCE.md` beside them). More collections can be added the same way.
- **Installed** read-only with the app's resources: `<prefix>/share/xournal-qt/stickers/` (Windows: `share\` next to
  `bin\`; macOS: the bundle's `Contents/Resources/share/`; Android: copied from the APK at start, anew when
  `collections.json` changed; a build tree: `build-qt/share/xournal-qt/stickers/`).
- **In the picker:** "Built in" lists the collections as chips (their titles), the stickers by their names in the
  app's language (`QLocale`: German shows "Widerstand", else "Resistor"); the search finds every name in both
  languages and the collection's title. While none was used they come in their own order (the collections', then
  `names.json`'s); "Date added" and "Own order" keep that order too.
- **Read-only:** the card's menu has only **Copy to my stickers** (a copy in the library's set, or the app-wide set
  without a library, at its root, named as shown; then it can be opened and changed). Nothing renames, moves,
  reorders or deletes a built-in sticker, and no order file is written beside them.
- **Hide a collection:** press and hold (or right-click) its chip → **Hide this collection**, or its switch in
  **Settings → Documents → Built-in stickers**. A hidden collection is not listed and not searched; nothing on disk
  changes. The picker says "2 collections hidden · **Show**"; Settings has **Restore hidden collections**. Kept in
  `settings.xml` (`xournalQt`: `hiddenStickerCollections`, the ids joined by `+`).
- **In the pen's colour** (a check box in "Built in", and in Settings): a built-in sticker is pasted in the current
  pen's colour (its ink; off: black, as drawn). Kept as `builtinStickersInPenColour`. The user's own stickers are
  pasted as they are.

## Managing stickers

The card's menu (press and hold, or right-click):

- **Rename**, **Move up** / **Move down** (the own order: the picker shows "Own order" then), **Move to folder…**;
- **Open** (the sticker as a document of its own, to change it; saved, the picker shows it as it is now);
- **Copy to all libraries** (from the library's set to the app-wide set) / **Copy to this library** (the other way),
  **Copy to library…** (another library in `<Documents>/Xournal_Libraries`: into its `Stickers/` folder);
- **Delete** (to the trash, as the library deletes).

## In the library

The `Stickers/` folder is an ordinary folder of the library: its stickers are documents (cards with previews, search
finds their text, they can be opened, shared, copied to another library with the library's "Copy to…"). Its folder
card says "Stickers" and has a sticker badge. Pictures dropped into it in a file manager are stickers at once.

## Code

| Where | What |
| --- | --- |
| `qt/src/session/StickerFile.*` | A sticker's document from content (`makeDocument`), writing it, reading one back as content (`read`), `recolour` (the pen's colour), the clipboard bytes. Qt-light, any thread. |
| `qt/src/shell/Stickers.*` | The sets on disk: where they are, listing (folders, stickers), unique names, own order (`.sticker-order.json`), last used, rename, move, copy, trash; the built-in set (`builtinSet`, `collections`: `names.json`, `collections.json`) and its settings (hidden collections, the pen's colour). `StickersModel`: the picker's list (scope, folder, search, sort; "builtin": titles, names in both languages, read-only, `copyToMine`, hiding). |
| `qt/resources/stickers/` | The built-in collections and their generator `generate.py`; copied into the resources by `XqtSession.cmake`, installed by `XqtPackage.cmake`. |
| `qt/src/canvas/CanvasStickers.cpp` (`CanvasView::stickerSource`, `saveSticker`, `loadSticker`, `pasteSticker`) | The selection as a sticker's content (copies; the lasso it was made with, `selectedWith` from `CanvasPage`), the picture and the file written or read off the UI thread (the view waits for it when it goes), the paste. |
| `MixedSelection::pasteAt`, `setClipboard` | Notes and elements pasted together at a point, made smaller only to fit the page, selected, one step; the clipboard entry with its picture for other apps. |
| `qt/src/app/AppStickers.cpp` | `app.stickers` (the model), `saveSticker`, `pasteSticker`, the card actions. |
| `StickerButton.qml`, `StickerPicker.qml`, `StickerSaveDialog.qml` | The button (self-contained, for the toolbox), the picker (with "Built in", the chips' menu, "Show", "In the pen's colour"), the dialog. `SelectionPill.qml` and `NotePill.qml`: "Save as sticker…". `SettingsDocuments.qml`: "Built-in stickers". |

Tests: `StickersTest`, `BuiltinStickersTest` (shell: every built-in sticker in the app's and upstream's loader, one
group, names, sizes; the scope, search, read-only, copy, hidden collections in the settings), `StickerFileTest`
(session), `StickerToolTest` and `BuiltinStickerToolTest` (ui).

## On the device

- The picker's three scopes beside "+ Save selection" on a phone's sheet (narrow: the tab titles may be cut short).
- Built-in stickers on Android: listed after the first start (copied from the APK), and anew after an update that
  changed them.
- A built-in sticker pasted with a pen, moved and scaled: its lines meet hand-drawn wires.

## Not built

- More built-in collections (the author: "with the option to add more later"); names in languages other than English
  and German.
- The picker's search does not look into the stickers' text (the library's search does, for the library's set).
- Markdown pictures of a sticker pasted into a `.xopp`: the boxes are pasted, their pictures are found only while the
  document they came from is open (as for a copied Markdown box).
