# Performance logging of the canvas

`XQT_PERF=1` makes the application write one line per second to its standard error output, while something happens:

```
XQT_PERF=1 build-release/xournal-qt 2> /tmp/xqt-perf.log
```

```
xqt-perf 1.0 s: input mouse 312 (claimed 0, hit test 0.05/0.31 ms) touch 0 pen 0 | scroll 312 -> visibility 61
(0.42/2.10 ms) | frames 59 sync 3.1/18.4 ms | tiles 480 previews 7 | geometry 0 (displays 0) | sharp 2 after 180/240 ms
```

Per second:

- **input**: the events the canvas saw. `mouse`/`touch`/`pen` is how many of each, `claimed` how many of the mouse
  events were for the canvas itself (a drag on a scroll bar is not). `hit test` is the average and the worst time of
  finding the item under the pointer, which only mouse events pay for.
- **scroll**: changes of the scroll position or the zoom, and how many of them led to a **visibility** update: the
  current page, the models of the sidebar and the overviews, the pages to render. Plain scrolling does this at most
  every 8 ms; a jump (to a page, a fit) right away. The times are the average and the worst one.
- **frames**: frames of the canvas and the time of their scene graph sync (composing and uploading page tiles happens
  there, and it blocks the UI thread), average and worst.
- **tiles**: page tiles composed and uploaded (256 x 256 px each), **previews** the page previews uploaded for pages
  that are not rendered yet.
- **geometry**: pictures of the setsquare or compass drawn (the canvas moves, turns and sizes them on the GPU; they are
  drawn anew only for a new size or zoom, once that has been stable for 150 ms), and in brackets its small angle
  display, drawn anew when the angle it shows changes. Moving and turning the tool should draw no picture and no
  page tile.
- **sharp**: pages in view that got their render at the zoom they are shown at, and how long they waited for it
  (average and worst) since the view last asked for it, that is since scrolling or zooming stopped. After a Ctrl+wheel
  zoom this includes the 300 ms the renders wait for the zoom to be stable; after a pinch it does not (lifting the
  fingers ends the zoom).

What the numbers say:

- `frames` well below 60 with a big `sync` worst time: the canvas is busy composing tiles; `tiles` shows how many.
- `scroll` much higher than `frames`: the input sends more than the canvas can show (a mouse sends more moves than
  there are frames); `visibility` should stay near `frames`.
- `sharp` high after a zoom or a jump: the page in view waits for its render (a heavy page, or other work in front
  of it); pages rendered in advance, previews and thumbnails wait while a page in view is rendered.
- `mouse` high with a large `hit test` worst time: the hit test of the item under the pointer is expensive.

## Window changes (`XQT_LOG_WINDOW=1`)
`XQT_LOG_WINDOW=1 xournal-qt` writes a line on stderr for every change of a window's state (maximized, full
screen, ...), size and position, for the touch and mouse presses on it, and for what the app itself asks of the
window ("app asks: ..."). A change with no "app asks" line just before it came from the compositor. It was added
for a flaky bug where a touch on the "pages with hits" filter made the maximized window half as high.
