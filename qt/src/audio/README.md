# audio: recordings

Target `xqt-audio` (`qt/cmake/XqtAudio.cmake`). Recording from the microphone and playing back, compatible with
Xournal++'s audio (a stroke or text tied to a moment of a recording). The window's side (`app.audio`) is
`app/AudioControl`.

| Class | Job |
| --- | --- |
| `Recorder` | records into an Ogg Vorbis file; its time is its count of samples |
| `Player` | decodes and plays a recording from a position |
| `OggVorbis` | reading and writing Ogg Vorbis with the vendored libogg/libvorbis |
| `AudioDevice`, `QtAudioDevice` | the microphone and the speaker behind small interfaces; Qt Multimedia's `QAudioSource`/`QAudioSink` |
| `FakeAudio` | fake devices for the tests and for `XQT_FAKE_AUDIO=1` |
| `AudioFiles` | where recordings are kept and found (as Xournal++ does) |
| `DocumentAudio` | the recordings of a document and the elements tied to them |

**May depend on**: `xoj-core` (upstream's model: the `ts`/`fn` of strokes and texts, `XojPage`'s voice memos; an undo
action), Qt Core, Qt Multimedia (optional: without it the app builds without recording; `XQT_REQUIRE_AUDIO=ON` makes
that an error). No views.

**Threads**: the devices run on the UI thread (mono 48 kHz is little data).

**Tests**: `qt/tests/audio` (label `audio`). **Docs**: [audio](../../docs/features/audio.md).
