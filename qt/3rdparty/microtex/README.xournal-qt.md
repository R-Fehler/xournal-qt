# MicroTeX (vendored)

Lays out the math formulas of the Markdown text (`$…$`, `$$…$$`; `qt/src/markdown/MdMath.*`).

- Upstream: https://github.com/NanoMichael/MicroTeX, branch `openmath` (the OpenType math rewrite that is called
  MicroTeX; `master` is the older cLaTeXMath code with its own TeX fonts).
- Version: commit `086f4eb740270b28bd0c61a0a359aea9300d61ae` (2024-08-05; its last fixes of the code are from
  2023-09). There is no release of this branch.
- Files: upstream's `lib/` as `src/` (without `wrapper/`, `microtexapi.h` and its build files; renamed because
  `lib/` folders are commonly ignored by git), `LICENSE`, and `res/lm-math/latinmodern-math.clm2` with its `README`.
- License: MIT (`LICENSE`). The math font, Latin Modern Math 1.959 (GUST e-foundry), is under the GUST Font License,
  an instance of the LaTeX Project Public License 1.3c (`res/lm-math/README`, `res/lm-math/GUST-FONT-LICENSE.txt`,
  from TeX Live). The `.clm2` is MicroTeX's form of it: the font's math tables and the outlines of its glyphs
  (converted by MicroTeX's `prebuilt/otf2clm.py`), so no font file is loaded at run time.

How it is built (`qt/cmake/XqtMarkdown.cmake`): a static library of every `src/**/*.cpp`, C++17, glyphs drawn as
paths only (`GLYPH_RENDER_TYPE=1`), no search for fonts on disk (`HAVE_AUTO_FONT_FIND` off). The `.clm2` is compiled
into the program as a C array (`qt/cmake/XqtEmbedFile.cmake`). MicroTeX's platform backends (cairo, Qt, …) are not
used: `MdMath.cpp` records what MicroTeX draws as Cairo paths.

## Changes to the sources

Marked with `xournal-qt:`.

- `src/microtex.cpp`: `MicroTeX::init` does not call `setlocale(LC_NUMERIC, "C")` (the app sets it at start; it
  must not change from a worker thread).
- `src/utils/utils.cpp`: `defaultLocale()` does not throw when `en_US.UTF-8` is not installed (Android, systems
  without it): it tries `C.UTF-8`, then falls back to the classic locale.
- `src/core/formula.cpp`: a parsed argument that is empty is an empty atom instead of a null one. Many atoms
  dereferenced it (`\raisebox{1pt}{}`, `\reflectbox{}`, `\shoveleft{}`, …: crashes found by fuzzing with random
  formulas). A consequence: `\frac{}{x}` draws an empty numerator instead of failing.
