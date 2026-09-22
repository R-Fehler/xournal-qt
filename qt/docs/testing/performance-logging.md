# Performance logging of the canvas

`XQT_PERF=1` makes the application write one line per second to its standard error output, while something happens:

```
XQT_PERF=1 build-release/xournal-qt 2> /tmp/xqt-perf.log
```

```
xqt-perf 1.0 s: input mouse 312 (claimed 0, hit test 0.05/0.31 ms) touch 0 pen 0 | scroll 312 -> visibility 61
(0.42/2.10 ms) | frames 59 sync 3.1/18.4 ms | tiles 480 previews 7
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

What the numbers say:

- `frames` well below 60 with a big `sync` worst time: the canvas is busy composing tiles; `tiles` shows how many.
- `scroll` much higher than `frames`: the input sends more than the canvas can show (a mouse sends more moves than
  there are frames); `visibility` should stay near `frames`.
- `mouse` high with a large `hit test` worst time: the hit test of the item under the pointer is expensive.
