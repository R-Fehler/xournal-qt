#!/usr/bin/env bash
# The portable Windows folder of xournal-qt, from a build made in MSYS2 UCRT64 (see qt/docs/development/windows.md):
#
#   qt/scripts/windows-deploy.sh <build dir> <program folder>      e.g.  build dist/xournal-qt
#
# What ends up in the program folder:
#   bin/                xournal-qt.exe, xournal-qt-cli.exe, the Qt and MinGW DLLs, Qt's plugins (platforms/, styles/,
#                       imageformats/, ...), the QML modules (qml/), qt.conf. Qt6Multimedia.dll for the recordings,
#                       without Qt's media plugins (multimedia/: FFmpeg and Windows Media Foundation players, not
#                       needed for audio in and out, see qt/docs/features/audio.md) and so without FFmpeg's DLLs
#   share/xournal-qt/   page templates, palettes, icons (AppContext looks for them next to bin/), the handwriting
#                       models (hwr-models/<name>/, from cmake --install)
#   bin/onnxruntime.dll ONNX Runtime for the handwriting search, with the Visual C++ runtime DLLs it imports
#   share/doc/xournal-qt/onnxruntime/   its LICENSE and ThirdPartyNotices.txt
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

# Recordings (qt/docs/features/audio.md, "Platforms"): QAudioSource and QAudioSink are in Qt6Multimedia.dll itself
# (WASAPI). windeployqt adds Qt's media plugins because the program links Qt Multimedia; they are for players, cameras
# and video, and the FFmpeg one would bring FFmpeg's DLLs (tens of MB) along. Taken out before the DLLs are collected.
step "Qt Multimedia: the library, without the media plugins"
if [[ -d "$bin/multimedia" ]]; then
    ls "$bin/multimedia"
    rm -rf "$bin/multimedia"
    echo "removed bin/multimedia (not needed for audio in and out)"
fi
# windeployqt also copies the FFmpeg plugin's own DLLs into bin/ (avcodec, avformat, avutil, swresample, ...): they
# go too, unless a binary that stays imports one of them
shopt -s nullglob
ffmpeg_dlls=("$bin"/avcodec-*.dll "$bin"/avformat-*.dll "$bin"/avutil-*.dll "$bin"/avfilter-*.dll "$bin"/avdevice-*.dll
             "$bin"/swresample-*.dll "$bin"/swscale-*.dll "$bin"/postproc-*.dll)
shopt -u nullglob
if (( ${#ffmpeg_dlls[@]} )); then
    others=$(find "$bin" -type f \( -name '*.dll' -o -name '*.exe' \) | grep -viE '/(avcodec|avformat|avutil|avfilter|avdevice|swresample|swscale|postproc)-[0-9]+\.dll$' || true)
    used=""
    while IFS= read -r f; do
        [[ -n "$f" ]] && used+=$(objdump -p "$f" 2> /dev/null | tr -d '\r' | sed -n 's/^[[:space:]]*DLL Name: //p')$'\n'
    done <<< "$others"
    for dll in "${ffmpeg_dlls[@]}"; do
        name=$(basename "$dll")
        if grep -qix "$name" <<< "$used"; then
            warn "$name is imported by a binary that stays: kept"
        else
            rm -f "$dll"
            echo "removed $name (FFmpeg, only for the media plugins)"
        fi
    done
fi
exe_imports=$(objdump -p "$bin/xournal-qt.exe" 2> /dev/null | tr -d '\r' || true)
if grep -qi 'DLL Name: Qt6Multimedia.dll' <<< "$exe_imports"; then
    echo "xournal-qt.exe uses Qt6Multimedia.dll: recording is built"
else
    warn "xournal-qt.exe does not use Qt6Multimedia.dll: this build offers no recording"
fi

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

# --- ONNX Runtime (the handwriting search; qt/docs/development/releasing.md, "Handwriting") --------------------------
# Microsoft's onnxruntime.dll (qt/packaging/onnxruntime.env) next to the program, where the app looks first
# (qt/src/hwr/OrtRuntime.cpp: never the older onnxruntime.dll that Windows 11 has in System32), with its licence
# files. It is built with Visual C++ and imports its runtime (MSVCP140*.dll, VCRUNTIME140*.dll), which a fresh
# Windows may not have: those DLLs go next to it, from Visual Studio's redistributable folder (app-local deployment
# as the Visual C++ licence allows; the runners have Visual Studio). The models (share/xournal-qt/hwr-models) came
# with `cmake --install`.
step "ONNX Runtime"
ort_dir=$(mktemp -d)
bash "$source_dir/qt/scripts/onnxruntime-fetch.sh" win-x64 "$ort_dir/rt"
cp "$ort_dir/rt/lib/onnxruntime.dll" "$bin/"
mkdir -p "$dist/share/doc/xournal-qt/onnxruntime"
cp "$ort_dir/rt/LICENSE" "$ort_dir/rt/ThirdPartyNotices.txt" "$dist/share/doc/xournal-qt/onnxruntime/"
rm -rf "$ort_dir"
vc_crt=""
vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if [[ -x "$vswhere" ]]; then
    while IFS= read -r d; do
        d=$(cygpath -u "$d")
        [[ -f "$d/msvcp140.dll" ]] && vc_crt=$d
    done < <("$vswhere" -latest -products '*' -find 'VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT' 2> /dev/null | tr -d '\r' | sort)
fi
if [[ -z "$vc_crt" ]]; then
    vc_crt=$(find "/c/Program Files/Microsoft Visual Studio" "/c/Program Files (x86)/Microsoft Visual Studio" \
        -path '*/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT/msvcp140.dll' 2> /dev/null | sort | tail -n 1 || true)
    vc_crt=${vc_crt%/msvcp140.dll}
fi
echo "Visual C++ runtime: ${vc_crt:-not found}"
while IFS= read -r dll; do
    case "${dll,,}" in
        msvcp140*.dll | vcruntime140*.dll | concrt140.dll) ;;
        *) continue ;;
    esac
    if [[ -n "$vc_crt" && -f "$vc_crt/${dll,,}" ]]; then
        cp "$vc_crt/${dll,,}" "$bin/$dll"
        echo "  $dll (from $vc_crt)"
    else
        system32_dll="$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")/System32/$dll"
        if [[ -f "$system32_dll" ]]; then
            cp "$system32_dll" "$bin/$dll"
            warn "$dll copied from System32 (no Visual Studio redistributable folder found)"
        else
            warn "$dll (imported by onnxruntime.dll) not found: the handwriting search needs the Visual C++ runtime"
        fi
    fi
done < <(objdump -p "$bin/onnxruntime.dll" 2> /dev/null | tr -d '\r' | sed -n 's/^[[:space:]]*DLL Name: //p')

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

Handwriting search (Settings -> Search): the built-in handwriting model in share\xournal-qt\hwr-models\ is for
non-commercial use only (the LICENCE.md in its folder); ONNX Runtime (bin\onnxruntime.dll, MIT, Microsoft) reads
it, its licence and notices are in share\doc\xournal-qt\onnxruntime\.

xournal-qt-debug.bat starts the program with a log of the pen, touch and mouse input (input-log.txt next to it), for
reporting problems with the pen.
EOF
# The input log starter (Windows wants CRLF in a batch file)
sed 's/\r*$/\r/' "$source_dir/qt/packaging/windows/xournal-qt-debug.bat" > "$dist/xournal-qt-debug.bat"

step "Summary"
du -sh "$dist"
find "$dist" -type f | wc -l
# Not fatal here: the folder is still published, and the smoke test shows whether the program starts.
if ((missing)); then
    echo "::warning::$missing imported DLL(s) missing from the folder (listed above)"
else
    echo "All imports resolved."
fi
