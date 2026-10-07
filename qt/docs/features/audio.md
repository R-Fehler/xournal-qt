# Audio recordings

The author: "audio recordings either associated with a page or even per stroke, keep it close to
upstream xournal++ audio compatibility. add the audio files as PDF attachments, like the xopp or md files, with the
page number in the filetitle so it's easy to recover/use even with archived PDFs when xournal stops existing in 30
years."

Decided with the author: Qt audio (`QAudioSource`/`QAudioSink`) with a bundled Ogg Vorbis
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
  documents are kept, saved and exported.

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
  upstream seam like `xqt-bookmark`, [ADR 0002](../decisions/0002-upstream-seams.md)). So a recording without ink is not lost,
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

## Storage

`qt/src/audio/AudioFiles.*`; `HybridPdf` (PDFs with notes and archives), `DocumentSave` (Save as).

- **`.xopp`, as upstream**: the strokes name their recordings bare (`fn="2026-10-04_14-03-22.ogg"`), the files are
  in the app's audio folder (`<app data>/audio`, e.g. `~/.local/share/xournal-qt/audio`; user data, never the cache).
  A recording is found, in this order: the name itself when it is an absolute path that exists; next to the
  document and in its `name.audio` folder; the app's audio folder; the recordings a PDF with notes brought (below);
  Xournal++'s audio folder when set in the settings (read only, `setExtraFolders`); folders added by the app.
- **PDFs with notes**: every recording the document refers to is an attachment named with its pages, so it is found
  without the app (the author: "with the page number in the filetitle so it's easy to recover/use even with archived
  PDFs"): `audio-p012-2026-10-04_14-03-22.ogg`, `audio-p012-p015-…` when it is on several pages (the first and last
  page with its memo or its ink, 1-based, three digits at least). Flat names (no folders, which many PDF apps do not
  show), MIME type `audio/ogg`, a description such as "Audio recording 2026-10-04_14-03-22, pages 12 to 15 (Ogg
  Vorbis; …)". No annotation is drawn for other PDF apps (decided). The marker lists them in `/Audio` (attachment
  name, name in the document) apart from `/Files`; the clean copy leaves them out.
  - The data is read from the recording's file while the PDF is written (a qpdf stream provider), not kept in memory.
  - **Incremental save** (Ctrl+S): a recording the file has stays as it is; when its pages changed (pages moved,
    inserted, deleted) its file specification is **renamed** (the same stream: the recording is not appended again);
    a new recording is appended. A recording removed from the document, or a new one in an archive PDF, makes the
    save a full write.
  - **Opening** takes the recordings out into the PDF's cache entry (`audio/`, next to the clean copy, once per version
    of the file); they are found for that PDF from there. "Save as" `.xopp` copies the recordings found only there
    into the app's audio folder, where the `.xopp`'s bare names find them.
  - Archive PDFs (PDF/A-3): associated files with `/AFRelationship /Supplement` in the catalog's `/AF`.
  - A recording whose file is nowhere is left out (its strokes keep their names); a log line says so.
- **Export for Xournal++**: the recordings are copied into `name.audio/` next to the exported `name.xopp`, and its
  strokes name them by their **absolute** paths (Xournal++ plays an absolute `fn` as it is), so Xournal++ plays them
  without setting its audio folder. Our app finds them there too (also after the two were moved together: by name in
  `name.audio/`). Voice memos stay bare names (Xournal++ does not read them).

Tests: `AudioStorageTest` (label `session`): the search order and attachment names; a PDF with notes carries the
recording (bytes unchanged, `audio/ogg`, the marker), `qpdf --check`, the clean copy without it, found again from the
PDF with the app's file gone; Ctrl+S after moving pages renames the attachment keeping its stream, and removing the
recording writes the file in full without it; an archive lists it in `/AF` as `/Supplement`; Export for Xournal++
copies it and writes absolute names; Save as `.xopp` puts it into the app's audio folder.

## In the app

`qt/src/app/AudioControl.*` (the QML object `app.audio`), `RecordButton.qml`, `RecordingPill.qml`,
`PlaybackPill.qml`, `RecordingsDialog.qml`. The QML is self-contained so the toolbox can place the button wherever
the user puts it.

- **Where the button is**: an app item of the arrangement (`record`): on the top bar in its first layout,
  wherever the user carries it (the rail, a group, off the bars: the catalog), in ⋮ → Tools, and in full screen the
  floating toolbox's ⋯ (it lists the top bar). Offered only when recording is available (a build with Qt Multimedia, or
  `XQT_FAKE_AUDIO=1`) and not in a text file; without it no bar, ⋮ or the catalog has a record button, and
  Ctrl+Shift+R does nothing. Ctrl+Shift+R starts and stops (the shortcut `record`: Settings → Shortcuts changes it).
- **The pills** (recording, playback) sit at the top of the page, in the middle; below the toolbox when it floats at
  the top edge (full screen, presenting).
- **Recording**: a tap starts recording for the document of this tab; the recording is a voice memo of the page shown
  then, and ink written meanwhile is tied to it. The red pill at the top of the canvas shows the time, the level,
  pause/resume and stop, and "No sound: is the microphone on?" when the first 5 s were silent. **The recording belongs
  to its tab**: in another tab the pill says whose it is ("for lecture.xopp"), ink there is not tied to it, and closing
  its tab ends it. One recording per window. Starting a recording stops what plays (the speaker would be recorded).
- **Playing**: held (or right-clicked), the button offers the **play tool** (upstream's `TOOL_PLAY_OBJECT`: a tap on
  ink with a recording plays it from its moment) and **Recordings of this document…** (each with its pages, length and
  amount of ink; play it, or remove it from the document: its ink stays, without the recording; undoable). Playing from
  ink starts **2 s earlier** (Settings → Documents → Audio recordings, 0–10 s), so the words before the ink are heard.
  The playback pill: 5 s back and forward, play/pause, a slider with a tick at every moment ink was written, the time,
  ×. Playing stops when another tab comes in front.
- Not built yet: upstream's fading of ink without a recording while the play tool is chosen (upstream's
  `DocumentView::setMarkAudioStroke`), a speaker chip on pages and thumbnails, "Play from here" in the selection pill,
  a field for Xournal++'s audio folder in the settings (`audio::setExtraFolders` is there).

Tests: `AudioUiTest` (label `ui`, fake devices with their timers): record from the record button, the pill, a
stroke tied to the recording and the page's memo, stop, the play tool on the stroke, the playback pill's pause and ×;
the list of recordings from the button's menu, removing one and undo; a recording belongs to its tab (another tab's
ink is not tied, closing the tab ends it); `NoAudioUiTest` (`audio::useNoDevices`, as a build without Qt Multimedia):
no record button anywhere (the bars, ⋮, the catalog), Ctrl+Shift+R does nothing.

## Platforms

Recording is offered on Linux, Windows, macOS and Android. Qt Multimedia is
LGPLv3/GPL like the other Qt modules, so no licence is in the way.

### Qt Multimedia without its media plugins

The recordings use only `QAudioSource` and `QAudioSink`. Since Qt 6.5 the audio devices are in the Qt Multimedia
library itself, on every platform (PulseAudio or PipeWire, WASAPI, Core Audio, AAudio/OpenSL ES); its plugins
(FFmpeg, Windows Media Foundation, AVFoundation, GStreamer, Android's media backend) are for players, cameras and
video. Checked with Qt 6.9 here: without any plugin Qt says "No QtMultimedia backends found. Only QMediaDevices,
QAudioDevice, QSoundEffect, QAudioSink, and QAudioSource are available." and the PulseAudio devices are in use.
So the packages leave the media plugins out, and with them FFmpeg (tens of MB, and a GPL build in MSYS2 and
Homebrew):

| Package | Qt Multimedia from | What it carries for recording |
| --- | --- | --- |
| Windows zip | MSYS2 `mingw-w64-ucrt-x86_64-qt6-multimedia` | `bin\Qt6Multimedia.dll`; `windows-deploy.sh` removes the `multimedia\` plugin folder windeployqt adds, so no FFmpeg DLLs follow |
| macOS `.dmg` | Homebrew `qtmultimedia` | `QtMultimedia.framework`; `macos-deploy.sh` removes `PlugIns/multimedia` and every library only those plugins used (FFmpeg and its codecs) |
| Android APK | aqt `-m qtmultimedia` | `libQt6Multimedia` and its Java part (devices); `qt_import_plugins(xournal-qt EXCLUDE_BY_TYPE multimedia)` keeps the media plugins out of the APK |
| Linux `.deb` | `qt6-multimedia-dev` (`linux-deps.sh`) | the distribution's Qt Multimedia, as a dependency |
| AppImage | the build machine's Qt | whatever linuxdeploy's Qt plugin bundles |

`XqtAudio.cmake` logs "recording is built" or "recording is NOT built" when configuring. The Windows, macOS and
Android jobs configure with `-DXQT_REQUIRE_AUDIO=ON`: a Qt without Qt Multimedia fails the build instead of shipping
a package without recording. `xournal-qt --audio-info` prints what recording runs on ("recording: available (Qt
Multimedia, Qt 6.x)", the microphones and speakers found; exit code 1 when recording is not offered); the Windows and
macOS smoke tests check it, that the Qt Multimedia library is in the package, and that no media plugin or FFmpeg
library is.

### The microphone permission

Asked before the first recording through Qt's `QMicrophonePermission` (`AudioControl::startRecording`): macOS and
Android show the system's question; Linux and Windows report it granted (on Windows the privacy switch "Let desktop
apps access your microphone" is not a Qt permission: switched off, the microphone cannot be opened and the recording
says so). The recording starts once the answer is yes. Refused, then or earlier, a dialog (`MicrophoneDialog.qml`)
says plainly that recording needs the microphone and where it is allowed, with **Open settings** where the system has
such a page: macOS's Privacy & Security → Microphone (`x-apple.systempreferences:` URL), Windows's privacy page
(`ms-settings:privacy-microphone`), Android's page of the app (`ACTION_APPLICATION_DETAILS_SETTINGS`, by JNI).

`AudioControl::setPermissionAccess` replaces the check and the question in tests; `XQT_FAKE_MIC_PERMISSION=denied`,
`ask-deny` or `ask-grant` pretends one in the app (to look at the dialog on Linux, with `XQT_FAKE_AUDIO=1`).

### macOS

- `NSMicrophoneUsageDescription` in `Info.plist.in` (without it macOS ends the app when the microphone is opened): the
  text of the system's question.
- The `.dmg` has an ad-hoc signature without the hardened runtime, which needs no entitlement for the microphone.
  Once it is signed with a Developer ID and the hardened runtime ([macos.md](../development/macos.md), "Signing and
  notarization"), the entitlements must include `com.apple.security.device.audio-input`.

### Android

- `RECORD_AUDIO` (asked at the first recording through `QMicrophonePermission`), `FOREGROUND_SERVICE`,
  `FOREGROUND_SERVICE_MICROPHONE`, `POST_NOTIFICATIONS`, `WAKE_LOCK` in the manifest. Min SDK 28, target SDK 36 as
  before.
- **A foreground service of type microphone while recording** (`RecordingService.java`): Android takes the microphone
  away from an app in the background otherwise. The native side reports every start, pause, resume and end
  (`AudioControl::setPlatformHook` in `main.cpp` → `XournalActivity.setRecording`); the service starts with the
  recording (from the record button, so the app is in front, as Android 14+ wants for a microphone service), is told
  of pauses and resumes, and stops when the recording ends. Swiping the app away ends the service; the recording file
  plays up to that moment (see "Format").
- **The notification**: "Recording", the document's title and the time (Android's chronometer, set from the recorded
  time, so paused time is not counted; while paused: "Recording paused · 4:07"), **Pause**/**Resume** and **Stop**.
  The buttons are intents to the service, which hands them to the native side (`XournalActivity.recordingCommand`,
  registered by `android::watchRecordingCommands`; `AudioControl::platformCommand`). A tap on the notification
  brings the app back. Its texts are translated on the native side and handed over.
- **Notifications permission** (Android 13+): asked once, at the first recording. Refused, the service runs all the
  same and its notification shows only in the task manager.
- **Screen off and background**: the service holds a partial wake lock while it runs (at most 12 h), and the
  activity's `android.app.background_running` meta-data keeps Qt's event loop running in the background, which takes
  the microphone's samples (Qt blocks it there otherwise, and the recording would have gaps or stop). The window draws
  nothing while it is not shown.

Tests (`AudioUiTest`, label `ui`): a refused microphone opens the dialog, nothing records, the microphone stays
closed and **Open settings** calls the settings; allowed later, the next tap records; a question first, recording on
yes, asked once for two taps, the dialog on no (without the settings button where there is no page); the platform's
hook hears start, pause (with the time), resume (from the same time) and stop, not the time alone, and the end of a
recording whose tab closes; the hook's commands pause, resume and stop. `RecorderTest.theDescriptionSaysWhatRecordingRunsOn`
(label `audio`): `--audio-info`'s text for each backend.

Not built or checked here: the Windows, macOS and Android packages (no SDKs in the build container; the next
release run builds them); the Java checked only against stubs of the Android API. See the device checklist.
