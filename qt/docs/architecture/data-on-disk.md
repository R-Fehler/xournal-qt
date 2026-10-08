# Where data lives on disk

Every file and folder the app writes outside the document a user opened, per platform, as the code finds it. What
each one is for is in the feature docs linked from each row; the words are those of the [glossary](glossary.md).
Nothing here holds the user's documents except the libraries and the app's own "Opened" and audio folders: the
config folder is the app's state, the cache folder can be deleted at any time.

## The base folders

The app's name is `xournal-qt` (`QGuiApplication::setApplicationName` in `main.cpp`; no organization name is set, so
Qt's app folders have no extra level). Two families of folders are used:

- **Upstream's helpers** (`src/util/PathUtil.cpp`): `Util::getConfigFolder()`, `getConfigSubfolder()`,
  `getConfigFile()` and `getCacheSubfolder()` are GLib's `g_get_user_config_dir()` and `g_get_user_cache_dir()`
  plus the folder name `xournal-qt` (`XOJ_CONFIG_FOLDER_NAME` in `qt/cmake/XojCore.cmake`; upstream's is
  `xournalpp`). Below, `<config>` and `<cache>`.
- **Qt's `QStandardPaths`**, used directly by a few parts of the app (below, by their names).

GLib follows the XDG variables on Linux and macOS. On Windows and Android the app points them at Qt's folders at
start, before anything reads them, and only when they are not set already: `WindowsSetup.cpp` (`XDG_CONFIG_HOME` =
`GenericConfigLocation`, `XDG_DATA_HOME` = `GenericDataLocation`, `XDG_CACHE_HOME` = `GenericCacheLocation`) and
`AndroidSetup.cpp` (`XDG_CONFIG_HOME` = `GenericConfigLocation`, `XDG_CACHE_HOME` = `CacheLocation`, `XDG_DATA_HOME`
= `AppDataLocation`, `XDG_STATE_HOME` = its `state/`, and `HOME` and `TMPDIR`).

| Base | Linux | Windows | macOS | Android (`/data/user/0/org.xournalqt.app/`) |
| --- | --- | --- | --- | --- |
| `<config>` | `~/.config/xournal-qt` | `%LOCALAPPDATA%\xournal-qt` | `~/.config/xournal-qt` | `files/settings/xournal-qt` |
| `<cache>` | `~/.cache/xournal-qt` | `%LOCALAPPDATA%\cache\xournal-qt` | `~/.cache/xournal-qt` | `cache/xournal-qt` |
| `AppDataLocation` | `~/.local/share/xournal-qt` | `%APPDATA%\xournal-qt` (Roaming) | `~/Library/Application Support/xournal-qt` | `files` |
| `AppConfigLocation` | `~/.config/xournal-qt` (= `<config>`) | `%LOCALAPPDATA%\xournal-qt` (= `<config>`) | `~/Library/Preferences/xournal-qt` | `files/settings` |
| `CacheLocation` | `~/.cache/xournal-qt` (= `<cache>`) | `%LOCALAPPDATA%\xournal-qt\cache` | `~/Library/Caches/xournal-qt` | `cache` |
| `GenericDataLocation` | `~/.local/share` | `%LOCALAPPDATA%` | `~/Library/Application Support` | Qt's external "user" folder (not checked on a device) |
| `DocumentsLocation` | `~/Documents` (the XDG user folder) | the user's Documents | `~/Documents` | the app's `Android/data/org.xournalqt.app/files/Documents` ([android.md](../development/android.md)) |

The concrete paths are those of Qt's documented `QStandardPaths` table and GLib's defaults; macOS and Windows were not
tried on a machine for this page. On Linux both GLib and Qt honour `XDG_CONFIG_HOME`, `XDG_CACHE_HOME` and
`XDG_DATA_HOME`; on Windows and macOS `QStandardPaths` ignores them (only GLib's folders move).

## The config folder (`<config>`): the app's state

Not a cache: deleting it resets the app.

| File | What | Code |
| --- | --- | --- |
| `settings.xml` | upstream's settings (`Settings`), written in place by upstream's code. The fork's own keys are in its custom elements `xournalQt` and `touch`, among them the toolbox (`toolbox`, JSON) and the shortcuts (`shortcuts`) | `AppContext.cpp`, `AppServices.cpp`, `ShortcutsModel.cpp` |
| `recent.json` | recent files and libraries | `RecentFiles.cpp` |
| `session.json` | the session journal: the open tabs, for recovery and the next start | `SessionRecovery.cpp` |
| `sessions/<library key>.json` | the journal of a window of another library than the default one (desktop, where each library has a window of its own); Android has one window and one `session.json` | `AppController::journalFileFor` |
| `documents/pages.json` | reading positions, title pages and favourites of documents outside a library, by their whole path | `DocumentPlaces.cpp` |
| `libraries/<library key>/library.json` | a library's settings: where its cache is kept (`cache`: `app` or `folders`), what it shows, its root | `Library.cpp` |
| `libraries/<library key>/pages.json` | reading positions, title pages and favourites of a library's documents, by their path in the library | `Library::placesFile` |
| `libraries/<library key>/stickers.json`, `templates.json` | when each sticker and template was last used (without a library: in `AppConfigLocation`) | `Stickers.cpp` |
| `edit-as-text.json` | the files the user agreed to edit as text | `AppTextFiles.cpp` |
| `library-move.json` | Android: the move of the libraries to the shared storage while it runs (finished at the next start) | `LibraryMigration.cpp` |

The **library key** is a short hash of the library's folder (`Library::key`): a library moved or renamed outside the
app gets a new key and starts without its positions and settings.

Palettes are not in the config folder: they are resources of the app (`palettes.json`, `xournal.gpl`). Upstream's
own config files (toolbar, `metadata/`) are not written: the code that writes them is not used by the Qt app.

## The cache folder (`<cache>`): can be deleted

| Folder | What | Code |
| --- | --- | --- |
| `autosaves/` | autosaves of unsaved documents, of every document in PDF files mode and on Android, and the files written at a crash: `<pid>-<tab>.autosave.xopp`, `.emergency.xopp`, `.autosave.pdf` (an encrypted document: encrypted again), `.autosave.text` and `.emergency.text` (text files). Recovery offers them at the next start | `DocumentSession.cpp`, `SessionRecovery.cpp` |
| `pages/` | the stand-ins kept for the next opening: a folder per document version (named by a hash of its path, its size and time and those of its PDF), a JPEG per page; up to 1 GB ([image-caches.md](image-caches.md)) | `PageSketches.cpp` |
| `previews/` | covers of documents outside a library (PNG) | `DocumentCovers.cpp` |
| `libraries/<library key>/<folder>/.xournal_library/` | a library's packs when it keeps its cache in the app cache (the default on Android; folders that cannot be written always) ([library.md](../features/library.md), "The library cache") | `LibraryCache.cpp` |
| `hybrid-pdf/` | the clean copies of opened PDFs with notes, with the pictures and recordings they carry, by a hash of the file's path and stamp | `HybridCache.cpp` |
| `pasted-pages/` | the merged PDF of pasted pages while its document is not saved yet | `MergedPdf.cpp` |
| `originals/<hash>/` | PDF files mode: the user's PDF as it was before it first became a PDF with notes, kept 30 days (Xournal++ files mode keeps `name.original.pdf` next to it instead) | `DocumentSave.cpp` |
| `versions/<pid>/` | versions taken out of a PDF's version history to be shown; other processes' leftovers go after a day | `VersionCache.cpp` |
| `md-assets/<hash>/` | the pictures of a Markdown document kept as PDF, unpacked while it is open | `DocumentImages.cpp` |
| `web-images/` | pictures of Markdown files fetched from the web | `MdImageDecoder.cpp` |
| `share/`, `share-work/` | copies handed to other apps (share, clipboard), and the work folder of a library shared as a zip | `AppController.cpp`, `LibraryShare.cpp` |
| `fontconfig/` | Windows: the `fonts.conf` written at every start and fontconfig's font cache (the CLI too) | `WindowsFonts.cpp` |

Two more caches use Qt's `CacheLocation`, which is `<cache>` on Linux only: the calendar files of to-dos
(`calendar/`, `TodoCalendar.cpp`; on Windows `%LOCALAPPDATA%\xournal-qt\cache\calendar`, on macOS
`~/Library/Caches/xournal-qt/calendar`), and on Android fontconfig's cache (`cache/fontconfig`).

## The app's data (`AppDataLocation`)

The user's own things that are not in a library:

| Folder | What | Code |
| --- | --- | --- |
| `audio/` | the recordings ([audio.md](../features/audio.md), "Storage") | `AudioFiles.cpp` |
| `stickers/`, `templates/` | the app-wide stickers and page templates (a library has its own `Stickers/` and `Templates/` folders) | `Stickers.cpp` |
| `Tutorial/Tutorial.pdf` | the copy of the tutorial that Help → Tutorial opens to write on | `AppHelp.cpp` |
| `Opened/` (translated) | files received from other apps when no library is open (else the library's `Opened/`) | `AppController::receivedFolder` |

The handwriting models downloaded or installed by the user are in `GenericDataLocation/xournal-qt/models/` (one
folder per model with its `model.json`; a download is staged in `<model>.part`), `HandwritingSearch::modelsDir`
([handwriting-search.md](../features/handwriting-search.md)). The models that **come with the app** are read-only
resources, `<resource dir>/hwr-models/<name>/` (`model.json`, its files, `LICENCE.md`;
`HandwritingSearch::bundledModelsDir`), from `qt/resources/hwr/` (XqtHwr.cmake):

| Where | Built-in models |
| --- | --- |
| Linux (`.deb`, AppImage, an install) | `<prefix>/share/xournal-qt/hwr-models/` |
| Windows (the zip) | `share\xournal-qt\hwr-models\` next to `bin\` |
| macOS | `xournal-qt.app/Contents/Resources/share/xournal-qt/hwr-models/` |
| Android | in the APK's resources (`:/xqt-share/hwr-models/`), copied at start to `files/share/xournal-qt/hwr-models/` (a model whose `model.json` changed is copied anew) |
| A build tree | `build-qt/share/xournal-qt/hwr-models/` |

On Android the app's data folder `files/` also holds the resources copied from the APK (`share/xournal-qt/`),
`fonts.conf`, the user's fonts (`fonts/`) and GLib's state folder (`state/`).

## Next to the user's documents

What the app may write beside a document or in a library's folders (each can be avoided or removed, see the feature
docs):

| File | When | Code |
| --- | --- | --- |
| `.xournal_library/` | a library that keeps its cache in its folders (the desktop's default): the packs of the documents in that folder | `LibraryCache.cpp` |
| `.name.autosave.xopp` | the autosave of a saved `.xopp` on the desktop in Xournal++ files mode | `DocumentSession::namedAutosavePath` |
| `name.xopp~` | during a save of a `.xopp` only (upstream's backup, removed when the save succeeded) | `DocumentSave.cpp` |
| `.name.<pid>-<n>.part` | during an atomic write only (renamed over the file) | `FileIo.cpp` |
| `name.xopp.bg.pdf`, `name.xopp.bg_N.png` | a `.xopp` whose PDF or background images are attached | `DocumentSave.cpp`, `DocumentFiles.cpp` |
| `.name.pages.pdf` (staged as `.name.next.pdf`) | a `.xopp` with PDF pages pasted from other PDFs ([page-files.md](../features/page-files.md)) | `MergedPdf.cpp`, `PdfPageKeeper.cpp` |
| `name.original.pdf` | Xournal++ files mode: a user's PDF the first time notes are saved into it | `DocumentSave.cpp` |
| `name.assets/` | the pictures of a Markdown file | `DocumentImages.cpp` |
| `name.audio/` | the recordings of a `.xopp` exported for Xournal++ | `AudioFiles.cpp` |
| `name.annotations.md` | the Annotations panel exported | `Annotations.cpp` |
| `.sticker-order.json`, `.template-order.json` | in a library's `Stickers/` and `Templates/` folders: their order | `Stickers.cpp` |
| `.xqt-moving-<name>/` | Android, while the libraries move to the shared storage | `LibraryMigration.cpp` |

There are no lock files: `FileWriteLock` is a lock inside the process. A PDF with notes keeps its notes, versions,
tags and recordings inside the PDF.

The **libraries** themselves are in `DocumentsLocation/Xournal_Libraries/` (the default one `Default/`), or any
folder opened as a library ([library.md](../features/library.md)); on Android the shared `Documents` folder once the
libraries were moved there ([android.md](../development/android.md)).

## Elsewhere

- **Temporary folders**: `QDir::tempPath()` with `xqt-import-…` (files imported through Android's picker),
  `xqt-received-…` (files received from other apps) and a temporary folder for printing. On Android `TMPDIR` is the
  app's cache folder.
- **Single instance**: a local socket (Unix) or named pipe (Windows) named `xournal-qt-<user>[-<library key>]`.
- **Logs**: none on disk. `XQT_PERF`, `XQT_LOG_INPUT` and the other switches write to stderr.
- **The CLI** (`xournal-qt-cli`) reads no settings and writes only the outputs it is given (a saved `.xopp` may get
  its `.bg.pdf`); on Windows also the fontconfig files above.

## Tests and other overrides

No `XQT_*` variable moves the config or the cache; the XDG variables do. The test binaries set `XDG_CONFIG_HOME`,
`XDG_CACHE_HOME` and `XDG_DATA_HOME` to a temporary folder in their `main.cpp` (the session tests only the cache), so
they never touch the author's folders ([testing/README.md](../testing/README.md)). This isolates Qt's folders on Linux
only, where the tests run. Some classes take a folder of their own for tests (`DocumentPlaces::setOutsideFile`,
`Library::setPlatformFolders`, `DocumentSession::setAutosaveInAppCache`, `audio::setAppFolder`, `stickers::setAppSet`).
`XQT_RESOURCE_DIR` points to the resources, `XQT_HWR_MODEL` and `XQT_HWR_MODEL_DE` to a handwriting model,
`XQT_ONNXRUNTIME` to the ONNX Runtime library (else Linux `<prefix>/lib/xournal-qt/libonnxruntime.so.1`, Windows
`bin\onnxruntime.dll`, macOS `Contents/Frameworks/libonnxruntime.1.dylib`, Android `libonnxruntime.so` among the
APK's native libraries; `OrtRuntime.h`).

The settings keys themselves are listed once they are typed in one table (TODO.md, infra B13).
