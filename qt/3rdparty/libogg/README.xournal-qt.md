# libogg (vendored)

The Ogg container of the audio recordings (`qt/src/audio/OggVorbis.*`, qt/docs/audio.md), used by libvorbis.

- Upstream: https://xiph.org/ogg/ (https://gitlab.xiph.org/xiph/ogg)
- Version: 1.3.6. Source: Xiph's release tarball `libogg-1.3.6.tar.xz`, taken from the Ubuntu archive as
  `libogg_1.3.6.orig.tar.xz` (http://archive.ubuntu.com/ubuntu/pool/main/libo/libogg/; xiph.org and GitHub could not
  be reached from the build machine). sha256 of the tarball as downloaded:
  `5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061`. Compare it with Xiph's published checksum
  (https://downloads.xiph.org/releases/ogg/SHA256SUMS) when updating.
- Files: `src/` and `include/` as released (without the autotools `Makefile.am`/`Makefile.in`), `COPYING`, `AUTHORS`,
  `CHANGES`, `README.md`. Not copied: the build systems, `doc/`, `win32/`.
- License: BSD-3-Clause (`COPYING`).

How it is built (`qt/cmake/XqtAudio.cmake`): a static library `xqt-ogg` of `src/bitwise.c` and `src/framing.c`;
`ogg/config_types.h` is made from `include/ogg/config_types.h.in` with the C99 fixed-size types, as libogg's own
CMakeLists.txt does. No changes to the sources.
