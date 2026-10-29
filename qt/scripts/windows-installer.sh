#!/usr/bin/env bash
# The Windows installer, from the program folder that windows-deploy.sh made (qt/docs/development/windows.md,
# "The installer"):
#
#   qt/scripts/windows-installer.sh <program folder> <output folder>
#
# writes <output folder>/xournal-qt-<version>-windows-setup-x64.exe, the version from qt/CMakeLists.txt. makensis is
# $MAKENSIS, the one on the PATH, or NSIS's own folder on Windows (GitHub's Windows runners have NSIS). It runs on
# Linux too, with Debian's or Ubuntu's nsis package: that checks the script (warnings are errors) but cannot try the
# installer, which windows-installer-test.sh does on Windows.
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <program folder> <output folder>" >&2
    exit 2
fi
dist=$(cd "$1" && pwd)
mkdir -p "$2"
out=$(cd "$2" && pwd)
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
nsi="$source_dir/qt/packaging/windows/xournal-qt.nsi"
version=$(sed -n 's/^project(xournal-qt VERSION \([0-9.]*\).*/\1/p' "$source_dir/qt/CMakeLists.txt")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "::error::no x.y.z version in qt/CMakeLists.txt" >&2; exit 1; }
setup="$out/xournal-qt-$version-windows-setup-x64.exe"

test -f "$dist/bin/xournal-qt.exe" || { echo "::error::$dist is not a program folder (no bin/xournal-qt.exe)" >&2; exit 1; }

# An update removes the installed version's program files by name (RemoveProgramFiles in the script): a new
# top-level entry of the folder must be added there, or the next update would leave it behind.
removed=$(sed -n '/^!macro RemoveProgramFiles/,/^!macroend/p' "$nsi")
missing=0
for entry in "$dist"/*; do
    name=$(basename "$entry")
    if ! grep -qF "\"\$INSTDIR\\$name\"" <<< "$removed"; then
        echo "::error::$name is in the program folder but not in RemoveProgramFiles of $nsi" >&2
        missing=1
    fi
done
((missing == 0)) || exit 1

makensis=${MAKENSIS:-}
if [[ -z "$makensis" ]]; then
    for c in makensis makensis.exe "/c/Program Files (x86)/NSIS/makensis.exe" "/c/Program Files (x86)/NSIS/Bin/makensis.exe" \
        "/c/Program Files/NSIS/makensis.exe"; do
        if command -v "$c" > /dev/null 2>&1; then
            makensis=$(command -v "$c")
            break
        elif [[ -x "$c" ]]; then
            makensis=$c
            break
        fi
    done
fi
[[ -n "$makensis" ]] || { echo "::error::makensis not found (install NSIS, or set MAKENSIS)" >&2; exit 1; }

# A Windows makensis wants Windows paths, and MSYS2 must leave its arguments alone.
native() { if command -v cygpath > /dev/null 2>&1; then cygpath -w "$1"; else echo "$1"; fi; }
export MSYS2_ARG_CONV_EXCL='*'

echo "makensis: $makensis ($("$makensis" -VERSION))"
echo "Xournal Qt $version from $dist"
rm -f "$setup"
"$makensis" -NOCD -WX -V3 \
    "-DVERSION=$version" \
    "-DSOURCE_DIR=$(native "$dist")" \
    "-DOUTFILE=$(native "$setup")" \
    "-DREPO_DIR=$(native "$source_dir")" \
    "$(native "$nsi")"
ls -l "$setup"
