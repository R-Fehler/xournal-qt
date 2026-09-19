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
- [ ] Add a page (+ page icon); page counter; scrolling through many pages stays smooth.
- [ ] Open a .xopp with a PDF background, and open a plain .pdf to annotate.
- [ ] Save / save as; the file then opens in upstream Xournal++ and looks identical.
- [ ] Closing or opening with unsaved changes asks first.
- [ ] Colors and the three sizes apply to pen and highlighter.

Report problems with the input log (see qt/spikes/inkpad/README.md) or a screen recording.
