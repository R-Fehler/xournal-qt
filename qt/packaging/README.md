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
- the icon (`hicolor/scalable/apps/xournal-qt.svg`) and the .xopp file icon;
- a Dolphin service menu for folders: "Open as Xournal Qt library" (right click on a folder).
  Folders are not registered as a file type of the application (`inode/directory` in the desktop file): that can
  make an application the default for opening folders.

The dependencies come from the libraries the program uses (`dpkg-shlibdeps`), plus the QML modules and the SVG
image plugin (package names of KDE neon / Ubuntu).
