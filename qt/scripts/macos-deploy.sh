#!/usr/bin/env bash
# The macOS app bundle and disk image of xournal-qt, from a build made with Homebrew's libraries (see qt/docs/macos.md):
#
#   qt/scripts/macos-deploy.sh <build dir> <output folder>      e.g.  build dist
#
# Writes <output folder>/xournal-qt.app and <output folder>/xournal-qt-<version>-macos-<arch>.dmg (the app and a link
# to /Applications). Unsigned: the bundle gets an ad-hoc signature only, which Apple Silicon needs to run it at all.
#
# The bundle:
#   Contents/MacOS/                 xournal-qt, xournal-qt-cli
#   Contents/Frameworks/            Qt's frameworks and every Homebrew library they and the program use (macdeployqt)
#   Contents/PlugIns/               Qt's plugins (cocoa, offscreen for scripted runs, SVG icons, image formats, ...)
#   Contents/Resources/qml/         the QML modules the QML files import (macdeployqt -qmldir)
#   Contents/Resources/share/xournal-qt/   page templates, palettes, icons, fonts (AppContext looks there)
#   Contents/Resources/xournal-qt.icns     the program's icon (qt/packaging/xournal-qt.svg)
#   Contents/Info.plist             from qt/packaging/macos/Info.plist.in
#
# Every step says what it does. The last ones check that no binary in the bundle still refers to a library outside it
# (a failure: the app would not start on a Mac without Homebrew) and verify the signature.
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <build dir> <output folder>" >&2
    exit 2
fi
build=$(cd "$1" && pwd)
mkdir -p "$2"
out=$(cd "$2" && pwd)
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)  # the checkout
qml_sources="$source_dir/qt/src/app/qml"
brew_prefix=$(brew --prefix 2> /dev/null || echo /opt/homebrew)

step() { printf '\n=== %s\n' "$*"; }
warn() { echo "::warning::$*"; }
error() { echo "::error::$*"; }

version=$(sed -n 's/^project(xournal-qt VERSION \([0-9.]*\).*/\1/p' "$source_dir/qt/CMakeLists.txt")
arch=$(uname -m)
[[ -n "$version" ]] || { error "no version in qt/CMakeLists.txt"; exit 1; }
app="$out/xournal-qt.app"
dmg="$out/xournal-qt-$version-macos-$arch.dmg"
step "xournal-qt $version for $arch -> $app"

rm -rf "$app" "$dmg"
contents="$app/Contents"
mkdir -p "$contents/MacOS" "$contents/Resources/share" "$contents/Frameworks" "$contents/PlugIns"

# --- The program and its resources ---------------------------------------------------------------------------------
step "Install the program into a staging folder, then into the bundle"
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT
cmake --install "$build" --prefix "$staging"
cp "$staging/bin/xournal-qt" "$contents/MacOS/"
if [[ -x "$build/xournal-qt-cli" ]]; then
    cp "$build/xournal-qt-cli" "$contents/MacOS/"
else
    warn "no xournal-qt-cli in $build (configure with -DXQT_BUILD_CLI=ON)"
fi
cp -R "$staging/share/xournal-qt" "$contents/Resources/share/"
printf 'APPL????' > "$contents/PkgInfo"

# --- The icon ------------------------------------------------------------------------------------------------------
step "The icon (qt/packaging/xournal-qt.svg -> xournal-qt.icns)"
iconset="$staging/xournal-qt.iconset"
mkdir -p "$iconset"
if command -v rsvg-convert > /dev/null; then
    for size in 16 32 128 256 512; do
        rsvg-convert -w "$size" -h "$size" "$source_dir/qt/packaging/xournal-qt.svg" -o "$iconset/icon_${size}x${size}.png"
        rsvg-convert -w $((size * 2)) -h $((size * 2)) "$source_dir/qt/packaging/xournal-qt.svg" \
            -o "$iconset/icon_${size}x${size}@2x.png"
    done
    iconutil -c icns "$iconset" -o "$contents/Resources/xournal-qt.icns"
else
    warn "no rsvg-convert (brew install librsvg): the bundle gets upstream Xournal++'s icon"
    cp "$source_dir/mac-setup/icon/xournalpp.icns" "$contents/Resources/xournal-qt.icns"
fi

# --- Qt ------------------------------------------------------------------------------------------------------------
qtpaths=$(command -v qtpaths6 || command -v qtpaths || true)
qt_plugins=$("$qtpaths" --query QT_INSTALL_PLUGINS 2> /dev/null || echo "$brew_prefix/share/qt/plugins")
macdeployqt=$(command -v macdeployqt6 || command -v macdeployqt || true)
if [[ -z "$macdeployqt" ]]; then
    for d in "$brew_prefix/opt/qtbase/bin" "$brew_prefix/opt/qttools/bin" "$brew_prefix/share/qt/libexec"; do
        if [[ -x "$d/macdeployqt" ]]; then
            macdeployqt="$d/macdeployqt"
            break
        fi
    done
fi
[[ -n "$macdeployqt" ]] || { error "macdeployqt not found"; exit 1; }
step "Qt: qtpaths=${qtpaths:-none}, plugins=$qt_plugins, macdeployqt=$macdeployqt"

# Plugins that macdeployqt does not pick by itself: the off-screen platform (scripted runs: the CI smoke test,
# XQT_SCREENSHOT) and the SVG plugins (the tool bar icons are SVG files; the program does not link Qt Svg itself).
# Put in first, and handed to macdeployqt with -executable so that they use the bundle's frameworks.
extra=()
for plugin in platforms/libqoffscreen.dylib iconengines/libqsvgicon.dylib imageformats/libqsvg.dylib; do
    if [[ -f "$qt_plugins/$plugin" ]]; then
        mkdir -p "$contents/PlugIns/$(dirname "$plugin")"
        cp "$qt_plugins/$plugin" "$contents/PlugIns/$plugin"
        chmod u+w "$contents/PlugIns/$plugin"
        extra+=("-executable=$contents/PlugIns/$plugin")
    else
        warn "not in this Qt: $qt_plugins/$plugin"
    fi
done
if [[ -f "$contents/MacOS/xournal-qt-cli" ]]; then
    extra+=("-executable=$contents/MacOS/xournal-qt-cli")
fi

step "macdeployqt"
# XournalQt is compiled into the program (a static QML module): macdeployqt may report it as not found, which is
# expected.
"$macdeployqt" "$app" -qmldir="$qml_sources" -verbose=1 ${extra[@]+"${extra[@]}"}

# --- What macdeployqt leaves out -------------------------------------------------------------------------------------
# macdeployqt copies the libraries that the program and Qt name by their full path, but not those that Homebrew's
# libraries name through @rpath (libpoppler-glib -> @rpath/libpoppler.164.dylib, with the rpath @loader_path/../lib,
# which in the bundle points nowhere: run 1 of the CI, 2026-09-27). Every such reference is pointed at
# Contents/Frameworks, and a library that is not there yet is copied from Homebrew, until nothing is left.
frameworks="$contents/Frameworks"
macho_files() { find "$contents" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \) -print0 |
    while IFS= read -r -d '' f; do file -b "$f" | grep -q 'Mach-O' && printf '%s\0' "$f"; done; }
deps_of() {  # the libraries a Mach-O file refers to, without its own name
    local id lib
    id=$(otool -D "$1" | tail -n +2)
    while IFS= read -r lib; do
        if [[ "$lib" != "$id" ]]; then
            echo "$lib"
        fi
    done < <(otool -L "$1" | tail -n +2 | awk '{print $1}')
}
rpaths_of() { otool -l "$1" | awk '/cmd LC_RPATH/{r=1} r && $1=="path"{print $2; r=0}'; }
# resolves <file> <reference>: the reference names a file in the bundle (or of macOS). @rpath: only Qt's frameworks
# (macdeployqt puts them into Frameworks, and the program has the rpath to it); any other @rpath is pointed at
# Frameworks explicitly.
resolves() {
    case "$2" in
        /System/* | /usr/lib/*) return 0 ;;
        @executable_path/*) [[ -e "$contents/MacOS/${2#@executable_path/}" ]] ;;
        @loader_path/*) [[ -e "$(dirname "$1")/${2#@loader_path/}" ]] ;;
        @rpath/*.framework/*) [[ -e "$frameworks/${2#@rpath/}" ]] ;;
        *) return 1 ;;
    esac
}
from_homebrew() {  # from_homebrew <library file name>: where Homebrew has it
    if [[ -e "$brew_prefix/lib/$1" ]]; then
        echo "$brew_prefix/lib/$1"
    else
        find -L "$brew_prefix/opt" -maxdepth 3 -name "$1" -print 2> /dev/null | head -n 1
    fi
}
step "Libraries that macdeployqt left outside the bundle"
unresolved=0
for pass in 1 2 3 4 5 6 7 8; do
    changed=0
    unresolved=0
    while IFS= read -r -d '' f; do
        while IFS= read -r lib; do
            if resolves "$f" "$lib"; then
                continue
            fi
            name=$(basename "$lib")
            case "$lib" in
                *.framework/*)
                    error "${f#"$contents"/} refers to $lib, which is not in the bundle"
                    unresolved=$((unresolved + 1))
                    continue
                    ;;
            esac
            if [[ ! -e "$frameworks/$name" ]]; then
                src=""
                if [[ "$lib" == /* && -e "$lib" ]]; then
                    src=$lib
                else
                    src=$(from_homebrew "$name")
                fi
                if [[ -z "$src" ]]; then
                    error "${f#"$contents"/} refers to $lib, which is neither in the bundle nor in Homebrew"
                    unresolved=$((unresolved + 1))
                    continue
                fi
                cp -L "$src" "$frameworks/$name"
                chmod u+w "$frameworks/$name"
                install_name_tool -id "@executable_path/../Frameworks/$name" "$frameworks/$name" 2> /dev/null
                echo "added $name from $src"
            fi
            echo "${f#"$contents"/}: $lib -> @executable_path/../Frameworks/$name"
            install_name_tool -change "$lib" "@executable_path/../Frameworks/$name" "$f" 2> /dev/null
            changed=1
        done < <(deps_of "$f")
    done < <(macho_files)
    echo "pass $pass: $([[ $changed == 1 ]] && echo "changes made" || echo "nothing to change")"
    if ((changed == 0)); then
        break
    fi
done
# rpaths into Homebrew or the build folder: dropped (they would find Homebrew's libraries on the build machine and
# hide a library missing from the bundle)
while IFS= read -r -d '' f; do
    while IFS= read -r rp; do
        case "$rp" in
            "$brew_prefix"/* | /opt/* | /usr/local/* | /Users/*)
                echo "${f#"$contents"/}: rpath $rp removed"
                install_name_tool -delete_rpath "$rp" "$f" 2> /dev/null
                ;;
        esac
    done < <(rpaths_of "$f")
done < <(macho_files)
for exe in "$contents/MacOS/xournal-qt" "$contents/MacOS/xournal-qt-cli"; do
    if [[ -f "$exe" ]] && ! rpaths_of "$exe" | grep -qx '@executable_path/../Frameworks'; then
        install_name_tool -add_rpath @executable_path/../Frameworks "$exe" 2> /dev/null
    fi
done

# --- Check: nothing refers to a library outside the bundle ---------------------------------------------------------
step "Libraries outside the bundle"
outside=$unresolved
while IFS= read -r -d '' f; do
    while IFS= read -r lib; do
        if ! resolves "$f" "$lib"; then
            error "${f#"$contents"/} refers to $lib"
            outside=$((outside + 1))
        fi
    done < <(deps_of "$f")
done < <(macho_files)
echo "$outside reference(s) to libraries outside the bundle"

# --- Info.plist: the version, and the oldest macOS every binary runs on --------------------------------------------
step "Info.plist"
minimum=0
while IFS= read -r -d '' f; do
    while IFS= read -r v; do
        # (numbers as major*100+minor to compare)
        n=$(awk -F. '{print $1 * 100 + $2}' <<< "$v")
        if ((n > minimum)); then
            minimum=$n
        fi
    done < <(otool -l "$f" | awk '/LC_BUILD_VERSION/{b=1} b && $1=="minos"{print $2; b=0} /LC_VERSION_MIN_MACOSX/{m=1} m && $1=="version"{print $2; m=0}')
done < <(macho_files)
minimum_system="$((minimum / 100)).$((minimum % 100))"
echo "the newest minimum of the binaries: macOS $minimum_system"
sed -e "s/@VERSION@/$version/g" -e "s/@MINIMUM_SYSTEM@/$minimum_system/g" \
    "$source_dir/qt/packaging/macos/Info.plist.in" > "$contents/Info.plist"
plutil -lint "$contents/Info.plist"

# --- Ad-hoc signature (Apple Silicon runs nothing unsigned; install_name_tool broke the linker's signatures) -------
step "Ad-hoc signature"
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict --verbose=2 "$app"

cat > "$out/README.txt" << EOF
Xournal Qt $version for macOS ($arch), a test build: not signed with an Apple Developer ID and not notarized.

Drag xournal-qt.app into Applications. macOS refuses to open it the first time ("cannot be opened" / "Apple could
not verify ..."): open System Settings -> Privacy & Security, scroll down and click "Open Anyway" (macOS 15 and
newer), or remove the quarantine flag in a terminal:

    xattr -dr com.apple.quarantine /Applications/xournal-qt.app

Needs macOS $minimum_system or newer. Settings are kept in ~/.config/xournal-qt, caches in ~/.cache/xournal-qt; the
default library is ~/Documents/Xournal_Libraries/Default.

xournal-qt.app/Contents/MacOS/xournal-qt-cli is the command line tool (PDF and PNG export, as xournalpp --create-pdf).
EOF

# --- The disk image ------------------------------------------------------------------------------------------------
step "Disk image $dmg"
dmg_root="$staging/dmg"
mkdir -p "$dmg_root"
cp -R "$app" "$dmg_root/"
ln -s /Applications "$dmg_root/Applications"
cp "$out/README.txt" "$dmg_root/"
# hdiutil fails now and then on the CI's machines ("Resource busy"): a few more tries
for attempt in 1 2 3 4 5; do
    if hdiutil create -volname "Xournal Qt $version" -srcfolder "$dmg_root" -fs HFS+ -format UDZO -ov "$dmg"; then
        break
    fi
    ((attempt == 5)) && { error "hdiutil create failed"; exit 1; }
    sleep $((attempt * 5))
done

step "Summary"
du -sh "$app" "$dmg"
find "$app" -type f | wc -l
if ((outside)); then
    error "$outside reference(s) to libraries outside the bundle (listed above): it would not start without Homebrew"
    exit 1
fi
