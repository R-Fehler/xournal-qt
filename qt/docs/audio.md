# Audio recordings (`qt/audio`)

The author (2026-10-04): "audio recordings either associated with a page or even per stroke, keep it close to
upstream xournal++ audio compatibility. add the audio files as PDF attachments, like the xopp or md files, with the
page number in the filetitle so it's easy to recover/use even with archived PDFs when xournal stops existing in 30
years."

Decided by the author (2026-10-04, TODO.md): Qt audio (`QAudioSource`/`QAudioSink`) with a bundled Ogg Vorbis
codec, mono at about 64 kbit/s; `.xopp` recordings in the app's audio folder with bare names, as upstream; voice
memos per page with the `xqt-audio` page attribute; flat attachment names with page numbers in PDFs with notes,
renamed when pages move; no visible speaker annotation for other PDF apps; "Export for Xournal++" copies the
recordings to `name.audio/` with absolute names; an Android foreground service while recording; a 2 s lead-in when
playing from a stroke, as a setting.

## Format

Recordings are **mono Ogg Vorbis** files, as Xournal++ writes them (it records through PortAudio and writes with
libsndfile; its fixture `test/files/packaged_xopp/audioAttachment/test.ogg` is Vorbis, mono, 48 kHz). Either app plays
the other's files.

- The codec is vendored: libogg 1.3.6 and libvorbis 1.3.7 in `qt/3rdparty/` (BSD-3-Clause, built by
  `qt/cmake/XqtAudio.cmake`). Qt Multimedia only moves samples (no FFmpeg plugin is needed or shipped).
- `qt/src/audio/OggVorbis.*`: `VorbisWriter` (mono, at the input device's rate, VBR quality 0.1: about 60 kbit/s for
  dense sound, less for speech with its pauses; under 30 MB an hour) and `VorbisReader` (any channels mixed down to
  mono, sample-accurate seeking, the length from the last page).
- **A recording cut short still plays**: the writer puts a page into the file at least once a second and flushes it,
  so after a crash or an empty battery the file plays up to about a second before the end.
- Paths are opened with the wide-character API on Windows (non-ASCII names).

Tests: `OggVorbisTest` (label `audio`): a sine through the file (exact length, SNR above 20 dB, bit rate), 16-bit
samples in odd chunks, seeking to the sample (a tone burst found 20 ms after seeking 20 ms before it, forwards and
back), a file copied while being written and one cut in the middle of a page, files that are no recording, upstream's
`test.ogg`, a non-ASCII path.
