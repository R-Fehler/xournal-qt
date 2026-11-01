# The README's pictures

The pictures of the [README](../../../README.md) are the app itself, photographed off-screen by UI tests on a demo
library, and composed here. Nothing in them is drawn by hand; to show a new screen, add a shot and run the script
again.

| Picture | What | Made by |
| --- | --- | --- |
| `slides.png` | lecture slides as a PDF with notes: handwriting, highlights, arrows; the pages beside them | `ReadmeShots.slidesWithNotes` |
| `adaptive.png` | the handwritten lab journal on a desktop, an upright tablet and a phone | `ReadmeShots.onEveryScreen` (three shots, side by side) |
| `search.png` | Ctrl+F finding handwriting in the lab journal | `ReadmeShots.handwritingFound` |
| `library.png` | the library with its folders and covers (cropped to the cards) | `ReadmeShots.theLibrary` |
| `markdown.png` | Markdown written on a page, formatted as it is typed | `MainWindowTest.shotOfMarkdown` |
| `plot.png` | the function plotter, its dialog open | `PluginsUiTest.readmeShotOfAPlot` |
| `icon.png` | the app's icon (`qt/packaging/xournal-qt.svg` as a PNG) | not made by the script |

The shots are in `qt/tests/ui/MainWindowTest.cpp` and `qt/tests/ui/PluginsUiTest.cpp`; they are skipped unless
`XQT_SHOTS` (and for `ReadmeShots`, `XQT_SHOTS_DEMO`) is set.

## Making them again

```sh
qt/docs/screenshots/make-shots.sh <build folder>     # e.g. build-qt; writes the pictures into this folder
```

It needs `xqt-ui-tests` in the build folder (any build type), node with Playwright, and python3 with the packages
`Hershey-Fonts` and `Pillow`. The picture of the handwriting search needs ONNX Runtime: the build's own, or
`XQT_ONNXRUNTIME=<libonnxruntime.so.1>` (`qt/scripts/linux-onnxruntime.sh` downloads it). The script runs three
steps, each of which can be run alone:

1. [demo/make-demo.sh](demo/make-demo.sh) `<folder>`: the demo library. Chromium (Playwright) prints
   [demo/slides.html](demo/slides.html) and [demo/paper.html](demo/paper.html) as PDFs, and
   [demo/make_demo.py](demo/make_demo.py) writes the documents with ink: the lecture with notes, a lab journal, a
   worked exercise, two Markdown notes. The handwriting is Hershey's single-stroke script font made to look written
   ([demo/hand.py](demo/hand.py)): no one's real notes are needed, and the built-in handwriting model reads it.
2. The shots: `XQT_SHOTS=<folder> XQT_SHOTS_DEMO=<the demo library> xqt-ui-tests
   --gtest_filter='ReadmeShots.*:MainWindowTest.shotOfMarkdown:PluginsUiTest.readmeShotOfAPlot'` (off-screen, no
   display needed). Each test copies the demo library into a folder of its own.
3. [compose.py](compose.py) `<the shots> <this folder>`: crops the library and puts the three screens side by side.

The pictures of 2026-11 were made in the CI's Ubuntu 22.04 container with Qt 6.11.2 (`qt/scripts/ci-container.sh
jammy`): its fonts (DejaVu) are those of the pictures.
