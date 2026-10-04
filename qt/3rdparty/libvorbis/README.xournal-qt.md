# libvorbis (vendored)

The Vorbis codec of the audio recordings (`qt/src/audio/OggVorbis.*`, qt/docs/audio.md): recordings are written as
mono Ogg Vorbis files, as Xournal++ writes them (through libsndfile there), and read back for playing.

- Upstream: https://xiph.org/vorbis/ (https://gitlab.xiph.org/xiph/vorbis)
- Version: 1.3.7. Source: Xiph's release tarball `libvorbis-1.3.7.tar.gz`, taken from the Ubuntu archive as
  `libvorbis_1.3.7.orig.tar.gz` (http://archive.ubuntu.com/ubuntu/pool/main/libv/libvorbis/; xiph.org and GitHub
  could not be reached from the build machine). sha256 of the tarball:
  `0e982409a9c3fc82ee06e08205b1355e5c6aa4c36bca58146ef399621b0ce5ab`. That is the checksum Xiph lists for
  `libvorbis-1.3.7.tar.gz` as far as known when vendoring (not fetched: compare it with
  https://downloads.xiph.org/releases/vorbis/SHA256SUMS when updating).
- Files: `lib/` (with `books/` and `modes/`) and `include/` as released (without the autotools `Makefile.am`/
  `Makefile.in` and `lib/CMakeLists.txt`), `COPYING`, `AUTHORS`, `CHANGES`. Not copied: the build systems, `doc/`,
  `examples/`, `test/`, `vq/`, `win32/`, `macosx/`, `symbian/`.
- License: BSD-3-Clause (`COPYING`).

How it is built (`qt/cmake/XqtAudio.cmake`): one static library `xqt-vorbis` with the codec, `vorbisenc.c` (the
encoder's modes) and `vorbisfile.c` (reading and seeking files), the same sources as upstream's three libraries.
`lib/barkmel.c`, `lib/psytune.c` and `lib/tone.c` are upstream's development tools and are not built. No changes to the
sources.
