#!/usr/bin/env bash
# The portable Windows folder of xournal-qt, from a build made in MSYS2 UCRT64 (see qt/docs/windows.md):
#
#   qt/scripts/windows-deploy.sh <build dir> <program folder>      e.g.  build dist/xournal-qt
#
# What ends up in the program folder:
#   bin/                xournal-qt.exe, xournal-qt-cli.exe, the Qt and MinGW DLLs, Qt's plugins (platforms/, styles/,
#                       imageformats/, ...), the QML modules (qml/), qt.conf
#   share/xournal-qt/   page templates, palettes, icons (AppContext looks for them next to bin/)
#   share/poppler/      poppler's encoding data (poppler finds it relative to its DLL)
#   lib/gdk-pixbuf-2.0/ gdk-pixbuf's image loaders
#   etc/fonts/          fontconfig's configuration (fontconfig finds it relative to its DLL; it lists C:\Windows\Fonts)
#
# Every step says what it does, and the last one checks that every DLL any binary imports is in bin/ or in Windows
# (a warning, not a failure: the smoke test decides).
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <build dir> <program folder>" >&2
    exit 2
fi
prefix=${MINGW_PREFIX:?run this in an MSYS2 MinGW shell (UCRT64)}
build=$(cd "$1" && pwd)
dist=$2
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)  # the checkout
qml_sources="$source_dir/qt/src/app/qml"

step() { printf '\n=== %s\n' "$*"; }
warn() { echo "::warning::$*"; }

rm -rf "$dist"
mkdir -p "$dist"
dist=$(cd "$dist" && pwd)
bin="$dist/bin"

step "Install the program into $dist"
cmake --install "$build" --prefix "$dist"
test -f "$bin/xournal-qt.exe" || { echo "::error::cmake --install did not put xournal-qt.exe into $bin"; exit 1; }

# --- Where Qt is ---------------------------------------------------------------------------------------------------
first_command() {
    local c
    for c in "$@"; do
        if command -v "$c" > /dev/null 2>&1; then
            command -v "$c"
            return 0
        elif [[ -x "$c" ]]; then
            echo "$c"
            return 0
        fi
    done
    return 1
}
qtpaths=$(first_command qtpaths6 qtpaths-qt6 qtpaths "$prefix/share/qt6/bin/qtpaths.exe" "$prefix/share/qt6/bin/qtpaths6.exe" || true)
qt_query() {  # qt_query QT_INSTALL_PLUGINS fallback
    local v=""
    if [[ -n "$qtpaths" ]]; then
        v=$("$qtpaths" --query "$1" 2> /dev/null | tr -d '\r' || true)
    fi
    if [[ -n "$v" ]]; then
        cygpath -u "$v"
    else
        echo "$2"
    fi
}
qt_plugins=$(qt_query QT_INSTALL_PLUGINS "$prefix/share/qt6/plugins")
qt_qml=$(qt_query QT_INSTALL_QML "$prefix/share/qt6/qml")
step "Qt: qtpaths=${qtpaths:-none}, plugins=$qt_plugins, qml=$qt_qml"
ls "$qt_plugins" || warn "no Qt plugin folder at $qt_plugins"

# --- windeployqt: Qt's DLLs, plugins and the QML modules that the QML files import ---------------------------------
windeployqt=$(first_command windeployqt6 windeployqt-qt6 windeployqt \
    "$prefix/share/qt6/bin/windeployqt.exe" "$prefix/share/qt6/bin/windeployqt6.exe" || true)
if [[ -n "$windeployqt" ]]; then
    step "windeployqt ($windeployqt)"
    # XournalQt and XournalQt.Canvas are compiled into the program (static QML module): windeployqt may report them
    # as not found, which is expected.
    if ! "$windeployqt" --verbose 1 --qmldir "$qml_sources" --no-translations --no-system-d3d-compiler \
        --no-opengl-sw "$bin/xournal-qt.exe"; then
        warn "windeployqt failed (see above); copying Qt's plugins and QML modules by hand instead"
    fi
else
    warn "windeployqt not found (looked for windeployqt6, windeployqt-qt6, windeployqt, $prefix/share/qt6/bin); copying Qt's plugins and QML modules by hand"
    ls "$prefix/share/qt6/bin" 2> /dev/null || true
fi

# What the app needs whether windeployqt ran or not. A folder that is missing is copied whole from Qt.
ensure_dir() {  # ensure_dir <from> <to>
    if [[ -d "$2" ]]; then
        return 0
    fi
    if [[ -d "$1" ]]; then
        echo "adding $2 (from $1)"
        mkdir -p "$(dirname "$2")"
        cp -r "$1" "$2"
    else
        warn "not in this Qt: $1"
    fi
}
ensure_file() {  # ensure_file <from> <to>
    if [[ -f "$2" ]]; then
        return 0
    fi
    if [[ -f "$1" ]]; then
        echo "adding $2"
        mkdir -p "$(dirname "$2")"
        cp "$1" "$2"
    else
        warn "not in this Qt: $1"
    fi
}
step "Qt plugins and QML modules the app needs"
ensure_file "$qt_plugins/platforms/qwindows.dll" "$bin/platforms/qwindows.dll"
# Off-screen: for scripted runs (the CI smoke test, XQT_SCREENSHOT), a few hundred KB.
ensure_file "$qt_plugins/platforms/qoffscreen.dll" "$bin/platforms/qoffscreen.dll"
# SVG: the tool bar icons and the program icon are SVG files; windeployqt only adds these when the program links
# Qt Svg, which it does not (the plugins load it).
ensure_dir "$qt_plugins/imageformats" "$bin/imageformats"
ensure_dir "$qt_plugins/iconengines" "$bin/iconengines"
ensure_dir "$qt_plugins/styles" "$bin/styles"
ensure_dir "$qt_plugins/tls" "$bin/tls"
for module in QtQml QtQuick; do
    ensure_dir "$qt_qml/$module" "$bin/qml/$module"
done
for module in Controls Controls/Material Controls/Basic Controls/impl Templates Layouts Dialogs Window Shapes; do
    ensure_dir "$qt_qml/QtQuick/$module" "$bin/qml/QtQuick/$module"
done
for f in imageformats/qsvg.dll iconengines/qsvgicon.dll; do
    ensure_file "$qt_plugins/$f" "$bin/$f"
done

# Qt finds its plugins and QML modules next to the program, whatever MSYS2's Qt was built for.
step "qt.conf"
cat > "$bin/qt.conf" << 'EOF'
[Paths]
Prefix = .
Plugins = .
QmlImports = qml
Qml2Imports = qml
Translations = translations
EOF
cat "$bin/qt.conf"

# --- The C libraries' data -----------------------------------------------------------------------------------------
step "Data of poppler, gdk-pixbuf and fontconfig"
copy_data() {  # copy_data <from> <to>
    if [[ -e "$1" ]]; then
        mkdir -p "$(dirname "$2")"
        cp -r "$1" "$2"
        echo "copied $1"
    else
        warn "missing: $1"
    fi
}
copy_data "$prefix/share/poppler" "$dist/share/poppler"
copy_data "$prefix/lib/gdk-pixbuf-2.0" "$dist/lib/gdk-pixbuf-2.0"
copy_data "$prefix/etc/fonts" "$dist/etc/fonts"

# --- The MinGW DLLs: everything that a binary in the folder imports, recursively ------------------------------------
# objdump reads the import tables without running anything (ldd would load the DLLs).
imports_of() { objdump -p "$1" 2> /dev/null | tr -d '\r' | sed -n 's/^[[:space:]]*DLL Name: //p'; }

step "MinGW DLLs from $prefix/bin"
declare -A seen=()
queue=()
while IFS= read -r -d '' f; do
    queue+=("$f")
    seen["$(basename "${f,,}")"]=1
done < <(find "$dist" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)
copied=0
while ((${#queue[@]})); do
    f=${queue[0]}
    queue=("${queue[@]:1}")
    while IFS= read -r dll; do
        [[ -z "$dll" ]] && continue
        key=${dll,,}
        [[ -n "${seen[$key]+x}" ]] && continue
        seen[$key]=1
        if [[ -f "$prefix/bin/$dll" ]]; then
            cp "$prefix/bin/$dll" "$bin/"
            echo "  $dll  (for $(basename "$f"))"
            queue+=("$bin/$dll")
            copied=$((copied + 1))
        fi
    done < <(imports_of "$f")
done
echo "$copied DLLs copied"

# --- Check: every import is in bin/ or part of Windows ---------------------------------------------------------------
step "Check the imports of every binary"
system32=$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")/System32
missing=0
while IFS= read -r -d '' f; do
    while IFS= read -r dll; do
        [[ -z "$dll" ]] && continue
        case "${dll,,}" in
            api-ms-win-* | ext-ms-*) continue ;;  # API sets, resolved by Windows
        esac
        if [[ -f "$bin/$dll" || -f "$(dirname "$f")/$dll" || -f "$system32/$dll" ]]; then
            continue
        fi
        echo "::warning::${f#"$dist"/} imports $dll, which is neither in bin/ nor in Windows"
        missing=$((missing + 1))
    done < <(imports_of "$f")
done < <(find "$dist" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)

cat > "$dist/README.txt" << 'EOF'
Xournal Qt for Windows (a test build, not an installer)

Start bin\xournal-qt.exe. Keep the folders together: the program finds its Qt libraries, page templates, palettes
and icons relative to bin\.

Settings are kept in %LOCALAPPDATA%\xournal-qt, caches in %LOCALAPPDATA%\cache\xournal-qt. The default library is
Documents\Xournal_Libraries\Default.

bin\xournal-qt-cli.exe is the command line tool (PDF and PNG export, as `xournalpp --create-pdf`).
EOF

step "Summary"
du -sh "$dist"
find "$dist" -type f | wc -l
# Not fatal here: the folder is still published, and the smoke test shows whether the program starts.
if ((missing)); then
    echo "::warning::$missing imported DLL(s) missing from the folder (listed above)"
else
    echo "All imports resolved."
fi
