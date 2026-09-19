# xournal-qt: a Qt 6 fork of Xournal++

This repository is a **git fork of [Xournal++](https://github.com/xournalpp/xournalpp)** with its full history.
It adds a new Qt 6 frontend: Qt Quick UI, several documents in tabs, tablet-first input, and Wayland-first support.
The frontend reuses Xournal++'s tested core: the document model, `.xopp`/`.xoj` I/O, cairo rendering, undo, and the tools.

All new code lives in [`qt/`](qt/). Build it with:

```sh
cmake -S qt -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-qt
```

Everything else in the tree is upstream Xournal++. The upstream GTK build (root `CMakeLists.txt`) is left untouched.

## Fork rules

These rules keep `git merge upstream/master` cheap.

1. **Never delete or move upstream files.**
   - GTK-only files stay in the tree and are simply not compiled by `qt/CMakeLists.txt`.
   - Deleting them would turn every upstream commit to those files into a modify/delete conflict.
2. **Never reformat upstream files.** Keep diffs minimal and local.
3. **Tag every fork edit in an upstream file** with a `xournal-qt:` comment.
   - Prefer `#ifdef XOJ_NO_GTK` guards over rewriting code.
   - List each touched file in [`qt/docs/adr/0002-upstream-seams.md`](qt/docs/adr/0002-upstream-seams.md).
4. **Prefer seams with upstream names.**
   - New interfaces (for example `UndoContext`) keep the method names of the GTK `Control` class, so method bodies stay byte-identical.
5. **Fork-owned code belongs under `qt/`.**
   - The only exceptions are small, upstreamable seams inside `src/` (new headers such as `ExportBackgroundType.h`).
6. **Licensing.**
   - Upstream files keep their GPL-2.0-or-later headers.
   - New files are GPL-2.0-or-later as well, so they can be upstreamed.
   - The combined application is distributed under GPL-3.0-or-later.
   - It becomes AGPL-3.0-or-later once the optional MuPDF backend is linked.

## Remotes and merging

```sh
git remote -v            # upstream = github.com/xournalpp/xournalpp (push disabled)
qt/tools/merge-upstream.sh
```

The script:
1. fetches upstream;
2. merges `upstream/master` into the current branch;
3. rebuilds the Qt build;
4. runs the test suites, including the golden-image comparison against a reference upstream build.

Merge roughly monthly.

## Where things are
- `qt/docs/adr/`: architecture decision records.
- `qt/spikes/`: throwaway experiments (M0 input and canvas spike).
- `qt/tools/`: developer tools (merge script, image diff, tablet logger).
- The implementation plan and milestone status are in `qt/docs/ROADMAP.md`.
