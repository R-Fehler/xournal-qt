# app: AppController, main and the QML UI

The application: `main.cpp` (target `xournal-qt`), the C++ behind the UI (compiled into `xqt-shell`) and the whole UI
in `qml/` (the QML module `xqt-ui`, URI `XournalQt`; `qt/cmake/XqtApp.cmake` lists its files).

| Part | Job |
| --- | --- |
| `main.cpp`, `EngineSetup` | sets up the application, the engine (the image providers, the context property `app`; the UI tests' fixture uses the same `EngineSetup`) and the window |
| `AppServices`, `OpenDocuments`, `BackgroundJobs` | what the windows of the process share: settings and tools (`AppContext`), the library and its lists, the open documents of every window, the work off the UI thread that quitting waits for ([ADR 0003](../../docs/decisions/0003-app-services.md)) |
| `AppController` (`AppController.cpp` and the `App*.cpp` files beside it, one per area) | one window's QML API, the context property `app`: its tabs (`TabManager`), the current document's properties and every action the UI offers |
| `CurrentDocument` | the current tab's session and view and their signals, relayed in one place |
| `WindowContext` | what a window's feature objects get from it: the services, its tabs, its current document (`AppController::windowContext`) |
| `AudioControl`, `TimelineControl` | recording and playing (`app.audio`), the replay (`app.timeline`) |
| `PluginControl` | plugins in the window (`app.plugins`, [plugins.md](../../docs/features/plugins.md)): their commands, the window's operations (selection, tool, view, files), the permission question, dialogs, the live dialog and its preview |
| `AndroidSetup`, `AndroidActivity`, `WindowsSetup`, `WindowsFonts` | what the app needs on Android and Windows before the core starts |
| `qml/Main.qml` | the window (`win`, about 1,100 lines): it holds the window's state objects and instantiates its parts, each a file of its own |
| `qml/WindowInsets.qml`, `ViewModes.qml`, `ChromeLayout.qml` | the window's state: safe areas and the keyboard (`win.insets`), full screen, Zen, read only, presenting (`win.modes`), where the chrome goes (`win.layout`) |
| `qml/WindowActions.qml` | what the window's parts ask the window to do (`win.actions`): the look-ups of selected text, pages as files and templates, and the dialogs `app` asks for |
| `qml/WindowShortcuts.qml`, `PageKeys.qml` | the window's keys, and the one ordered Esc/Back dispatcher ([zen.md](../../docs/features/zen.md), "Esc and Back") |
| the other `qml/*.qml` | the parts: the tab strip, top bar and toolbox (`TabStrip`, `Toolbox`, `AppButtons`, `ToolboxMenus`, `MoreMenu`), the canvas's pills (`SelectionPill`, `NotePill`, `ViewPill`, …), sheets and dialogs (`AdaptiveDialog`, `MenuSheet`, …), the home screen (`HomeView.qml` and its parts: `HomeHeader`, `LibraryGridPage`, `DocumentGrid` (the library's and Recent's grid), …) and Settings (`SettingsPage.qml`, its sections `Settings*.qml` and row types) |

**May depend on**: everything below it (shell, quick, hwr, canvas, session, …). Nothing depends on it. From upstream:
`control/settings`, `control/ToolHandler`, the model and undo, mostly through the lower modules.

**Rules**: QML talks to C++ through `app` and `win` (context properties: undocked windows share one engine, each
window with its own `app`; every part is made in the window's context, so `win` is always there: no `typeof win`
guards, a missing member fails loudly; `QmlApiTest` checks every `app.*` name the QML uses against the C++
meta-objects); QML items that a test needs carry an `objectName`; no Qt API newer than 6.7 in QML, and no property
names that are JS globals.

**Known debt** (review 2026-10: [app-cpp.md](../../docs/review/2026-10/app-cpp.md), [qml.md](../../docs/review/2026-10/qml.md)):
`AppController` is still one class of many thousand lines; its feature objects move out in later rounds (TODO.md).

**Tests**: `qt/tests/ui` (label `ui`: the real window off-screen, the shared fixture `UiFixture.h`); `qt/tests/shell`
links the controller too. **Docs**: [adaptive layout](../../docs/features/adaptive-layout.md),
[toolbox](../../docs/features/toolbox.md), [zen](../../docs/features/zen.md),
[onboarding](../../docs/features/onboarding.md).
