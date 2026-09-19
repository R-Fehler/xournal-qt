# qt/compat: compile the upstream Xournal++ core without GTK

These headers come **before** the upstream include directories in the Qt build (see `qt/cmake/XojCore.cmake`).

- `gtkshim/`: `<gtk/gtk.h>` and `<gdk/gdk.h>` replacements. They contain plain data types with GDK 3 values and pure-cairo helpers ported from GDK 3. Using any real GTK API in code compiled by the Qt build is a compile error by design. Those spots need either a seam or a replacement.
- `include/`: shadow headers with the same path and API as an upstream header, but a GTK-free implementation. Example: `util/XojMsgBox.h`.
- `*.cpp`: implementations of shadow headers, or GTK-free implementations of upstream headers (`VersionInfo.cpp`).

When upstream code in the allowlist starts calling a GTK function or a new method of a shadowed class, the Qt build fails to compile. Extend the shim or shadow header, and record the change in [ADR-0002](../docs/adr/0002-upstream-seams.md).
