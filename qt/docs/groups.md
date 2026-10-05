# Groups of elements (`qt/groups`)

Elements that belong together (a drawn figure, a figure and its labels, a pasted sticker) are made a **group**: a
tap on any of them selects all of them, and they move, scale, rotate, copy and delete together. Groups are flat (an
element is in one group or in none) and lie in one layer, like a selection of elements.

## Using them

- **Group:** select the elements (rectangle, lasso, Select more), then **Ctrl+G** or the selection pill's **Group**
  button. One undo step ("Group").
- **Ungroup:** select the group (a tap on any member), then **Ctrl+Shift+G** or the pill's **Ungroup** button. One undo
  step ("Ungroup"). When the selection is one group, the pill shows only Ungroup in the place of Group; a selection
  of a group and other elements shows both (Group makes one new group of all of it, Ungroup undoes every group in it).
- **Selecting:** a tap on any member (select tools, the object select tool), a rectangle or lasso that takes in any
  member, and Select more (Ctrl/Shift + tap, or the pill's toggle) take the whole group; a second tap with Select more
  takes the whole group away again. A tap on a sticky note's element inside the note does the same in the note.
- **Pasted stickers are groups** ([stickers.md](stickers.md)): the sticker's ink and pictures are one group in the
  page's layer, its Markdown boxes another in the page's Markdown layer (a group does not span layers). A group saved
  inside a sticker gives way to the sticker's group. Ungroup takes a pasted sticker apart.
- The eraser: the pieces a stroke is cut into stay in its group. A group's members can still be erased one by one.
- Not in a document opened for reading only, nor in the reference while it is not written in.

## How it is stored

- **`.xopp`**: the element attribute `xqt-group="n"` on `stroke`, `text`, `image`, `teximage` and `link`, written only
  on elements in a group. The number identifies the group within its layer. Upstream Xournal++ looks attributes up by
  name and ignores this one: it opens the file without a message and shows every element; when it saves, it drops the
  attribute (the elements stay, ungrouped). The upstream seam (about 25 lines in 9 files, each marked `xournal-qt:`):
  `Element::getGroup/setGroup` (copied with the element; `Stroke::applyStyleFrom` for strokes and the eraser's
  pieces), `XmlAttrs.h`, the generic read in `XmlParser::parserStartElement`, a virtual with an empty default in
  `DocumentBuilderInterface` implemented by `LoadHandler`, and the write in `SaveHandler::visitLayer` (so autosave,
  recovery, sticker files and the `.xopp` inside a PDF with notes keep groups too). See
  [adr/0002-upstream-seams.md](adr/0002-upstream-seams.md).
- **Numbers:** a new group gets a number larger than every number in the document and every number handed out
  before while the app runs (`groups::fresh`), so a group brought back by undo never meets a new one with its number.
- **The clipboard:** upstream's `application/xournal` data stays exactly as upstream writes it (the group is not in
  `Element::serialize`), so a selection copied here pastes into Xournal++ as before. Beside it the fork writes
  `application/x-xournal-qt-groups`: the group number of each element in the same order, as text (`"3 3 0 7"`), only
  when something copied is grouped. The fork's own formats (a sticky note, notes with elements) carry the number after
  each element (format names `StickyNote3`, `StickyGroup2`; data copied by an older version is not pasted).
- **Pasting** gives every copied group a new number (`groups::renumber`): a pasted copy never joins the group it was
  copied from. Data from Xournal++ (no fork entry) is pasted ungrouped.
- **Moving into another layer** (a selection dragged onto another page, several notes with elements dropped on
  another page, elements moved into a sticky note): where a group of that layer already has the number (a duplicated
  page), the arriving group gets a new one (`groups::separate`, when the selection ends), so two groups never merge by
  accident.

Code: `qt/src/session/ElementGroups.*` (numbers, members, undo step, clipboard numbers), `qt/src/canvas/CanvasGroups.cpp`
(group / ungroup the selection), the selection paths in `CanvasPage` (tap, rectangle, lasso, in a note) and
`CanvasView` (`selectTogether`, `toggleSelected`, `clearSelection`), `MixedSelection` (paste, drop on another page),
`StickerFile::read` (a sticker as a group). Tests: `GroupsTest` (session: the attribute, a loader without groups, the
helpers), `GroupsCanvasTest` (canvas: group/ungroup with undo, selecting, moving, the clipboard, stickers).

## Not (yet)

- Nested groups; a group across layers (a sticker with Markdown boxes is two groups); sticky notes in a group (a note
  is a layer of its own: several notes with elements are selected together with Select more, as before).
- A group drawn differently while not selected (no frame around it); a member edited alone without ungrouping (a text
  is still edited with the text tool).
