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

## Devices

`qt/src/audio/AudioDevice.h`: the microphone (`AudioInput`, gives mono float samples) and the speaker (`AudioOutput`,
pulls mono float samples at the file's rate) behind small interfaces. Three backends:

- **Qt Multimedia** (`QtAudioDevice.cpp`): `QAudioSource` asked for mono float at the microphone's own rate (else
  mono 16-bit, else its preferred format mixed down); `QAudioSink` at the file's rate in mono, else the device's
  preferred format with linear resampling and the sample copied to every channel. Built when `XQT_AUDIO` is on (the
  default) and `Qt6::Multimedia` is found; no FFmpeg plugin is needed, as Qt only moves samples.
- **Fakes** (`FakeAudio.h`): a tone for the microphone, a counter for the speaker, driven by a 10 ms timer or moved on
  by hand in tests. `XQT_FAKE_AUDIO=1` makes the app use them (UI tests; trying the UI in a build without Qt
  Multimedia).
- **None**: a build without Qt Multimedia does not offer recording (the record button is hidden), but recordings in
  documents are kept, saved and exported as before.

Both run on the UI thread: mono 48 kHz is little data, and encoding 10 ms costs well under a millisecond.

### Recording and playing

- `Recorder` writes what the microphone gives into a `VorbisWriter`. **Its time is its count of samples**:
  `positionMs()` is the length of the file so far, and that is the timestamp a stroke made now gets (upstream's `ts`,
  milliseconds from the start of its recording). Paused time is not in the file and not in the timestamps. It
  reports a level (the peak of the last 50 ms) and **warns when the first 5 s are silent** (below about -50 dBFS: a
  muted or wrong microphone), until sound comes. A device that fails (unplugged) ends the recording; the file keeps
  what came before.
- `Player` decodes while the speaker asks. Its position is where it started plus what the device has played (not what
  is buffered); a seek restarts the device, so nothing from before the seek is heard after it. Pause, resume, seek
  while paused, a signal at the end.

Tests: `RecorderTest`, `PlayerTest` (fakes moved by hand: the file's length is the samples given, pause, level,
the silence warning and its end, a microphone that cannot be opened or is unplugged, no backend; the position of the
player against what the fake speaker gets, pause/resume/seek forwards, back and beyond the end, a missing file, an
unplugged speaker; one test each with the fakes' timers). `RecorderTest.theRealMicrophone` records a second from the
real microphone with `XQT_AUDIO_DEVICE=1` in a build with Qt Multimedia (skipped otherwise).

## In the document

`qt/src/audio/DocumentAudio.*`, `DocumentSession` ("audio recordings").

- **Strokes and texts, as upstream**: while a recording runs for a document (`DocumentSession::setRecording`, set by
  the app for the tab that records), every **pen** stroke (shapes drawn with the pen too) and every **new text** gets
  the recording's name (`fn`) and the time in it (`ts`, ms) when it is started, exactly where upstream does it
  (`InputHandler::createStroke`, `TextEditor`; here `CanvasPage` and `TextEditor`). The highlighter and the eraser
  do not, as upstream. The recognised shape of a stroke keeps it (`Stroke::applyStyleFrom`).
- **Voice memos**: a recording is also tied to the page it was started on (`DocumentSession::addVoiceMemo`, one undo
  step "Record audio"). It is the page attribute `xqt-audio="2026-10-04_14-03-22.ogg|…"` (`XojPage::getAudioMemos`, an
  upstream seam like `xqt-bookmark`, [ADR 0002](adr/0002-upstream-seams.md)). So a recording without ink is not lost,
  and it follows its page when pages move; a duplicated page keeps it, as its strokes keep theirs. Xournal++ ignores
  the attribute and drops it when it saves; the strokes' recordings keep working there.
- **Names**: as upstream, the time the recording started, `2026-10-04_14-03-22.ogg` (`-2`, `-3` … when taken), written
  bare into the file and found in the audio folder (see "Storage").
- `recordingsOf(doc)`: every recording a document refers to, with its pages (memos and elements), its count of
  elements and their first and last moments; `momentsOf(doc, name)`: its elements in time order (the ticks on the
  playback slider); `hitAt(page, x, y)`: what the play tool plays (the nearest visible element with a recording within
  15 points, upstream's radius).
- **Removing a recording** from a document (`DocumentSession::removeRecording`) clears the `fn`/`ts` of its strokes and
  texts and its memos in one undo step. The file stays where it is (another document may use it; upstream never
  deletes recordings either).

Tests: `AudioDocumentTest` (label `canvas`): pen strokes and pen shapes stamped through replayed tablet input, the
highlighter and strokes after the recording not, undo/redo keeping the stamp; a new text stamped at the moment its box
opened; memos and removing as one undo step each; the `.xopp` round trip (the raw XML: `ts`/`fn` as upstream writes
them and `xqt-audio` on the page and its duplicate); upstream's `old.xopp` with its recording; the play tool's hit
(nearest, too far, no recording, hidden layer); the names.
