# md4c (vendored)

Markdown parser used by the Markdown boxes (`qt/src/markdown`).

- Upstream: https://github.com/mity/md4c
- Version: 0.5.2. Source: `release-0.5.2.tar.gz`, sha256 `55d0111d48fb11883aaee91465e642b8b640775a4d6993c2d0e7a8092758ef21`.
- Files: `src/md4c.c`, `src/md4c.h` and `LICENSE.md`, copied unchanged.
- License: MIT (see `LICENSE.md`).

Why it is vendored: this pins the version the source mapping relies on. Text callbacks point into the input buffer, except for line breaks, indentation and NUL replacements. It also builds the same way on every platform.
