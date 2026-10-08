# Packaging

Following upstream Xournal++'s packaging (`readme/LinuxBuild.md`: CPack), a `.deb`:

```sh
cmake -S qt -B build-deb -G Ninja -DCMAKE_BUILD_TYPE=Release -DXQT_BUILD_TESTS=OFF -DXQT_BUILD_SPIKES=OFF
cmake --build build-deb --target package
sudo apt install ./build-deb/packages/xournal-qt_*.deb
```

It installs:
- `/usr/bin/xournal-qt` and its resources in `/usr/share/xournal-qt` (page templates, palettes, icons);
- `xournal-qt.desktop`: in the menu, and "Open with" for PDF, .xopp and .xoj files;
- `xournal-qt.xml`: the Xournal++ file types (known also without Xournal++);
- the icon (`hicolor/scalable/apps/xournal-qt.svg`) and the .xopp / .xoj file icons (as
  `xournal-qt-application-x-*.svg`);
- the handwriting models in `/usr/share/xournal-qt/hwr-models/<name>/`, each with its `LICENCE.md` (from
  `qt/resources/hwr/`);
- ONNX Runtime for the handwriting search, `/usr/lib/xournal-qt/libonnxruntime.so.1`, with its licence files in
  `/usr/share/doc/xournal-qt/onnxruntime/`. `cmake --install` does not put it there: the release job stages it with
  `qt/scripts/linux-onnxruntime.sh` (the version and checksums are in [onnxruntime.env](onnxruntime.env)) and hands
  the folder to CPack (`cpack -G DEB -D "CPACK_INSTALLED_DIRECTORIES=<staged folder>;."`). Built as above, without it,
  the package works and the handwriting search says ONNX Runtime is missing
  ([releasing.md](../docs/development/releasing.md#handwriting-onnx-runtime-and-the-model));
- a Dolphin service menu for folders: "Open as Xournal Qt library" (right click on a folder).
  Folders are not registered as a file type of the application (`inode/directory` in the desktop file): that can
  make an application the default for opening folders.

Every installed file has a name of its own (`xournal-qt…`), so the package can be installed next to the xournalpp
package (Xournal++ GTK). `packaging/check-conflicts.sh <deb>` compares the files with those of installed packages.

The dependencies come from the libraries the program uses (`dpkg-shlibdeps`), plus the QML modules and the SVG
image plugin (package names of KDE neon / Ubuntu).
