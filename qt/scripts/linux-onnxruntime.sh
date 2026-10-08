#!/usr/bin/env bash
# ONNX Runtime into a Linux install prefix, where the app looks for it (qt/src/hwr/OrtRuntime.cpp:
# <exe>/../lib/xournal-qt/libonnxruntime.so.1). The .deb and the AppImage jobs of xqt-release.yml run it:
#
#   qt/scripts/linux-onnxruntime.sh <prefix>        e.g.  AppDir/usr, or a staging folder for CPack
#
#   <prefix>/lib/xournal-qt/libonnxruntime.so.<ver>     the library (from qt/packaging/onnxruntime.env)
#   <prefix>/lib/xournal-qt/libonnxruntime.so.1         a link to it (the name the app opens)
#   <prefix>/share/doc/xournal-qt/onnxruntime/          its LICENSE (MIT) and ThirdPartyNotices.txt
#
# The download is checked by qt/scripts/onnxruntime-fetch.sh (XQT_DOWNLOAD_CACHE keeps it).
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <prefix>" >&2
    exit 2
fi
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
prefix=$1
ort=$(mktemp -d)
trap 'rm -rf "$ort"' EXIT

bash "$here/onnxruntime-fetch.sh" linux-x64 "$ort/rt"
install -d -m 0755 "$prefix/lib/xournal-qt" "$prefix/share/doc/xournal-qt/onnxruntime"
cp -P "$ort/rt/lib/"libonnxruntime.so.* "$prefix/lib/xournal-qt/"
chmod 0644 "$prefix/lib/xournal-qt/"libonnxruntime.so.*.*
install -m 0644 "$ort/rt/LICENSE" "$ort/rt/ThirdPartyNotices.txt" "$prefix/share/doc/xournal-qt/onnxruntime/"
ls -l "$prefix/lib/xournal-qt" "$prefix/share/doc/xournal-qt/onnxruntime"
