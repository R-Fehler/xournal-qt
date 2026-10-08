#!/usr/bin/env bash
# ONNX Runtime for one package (the handwriting search loads it at run time: qt/src/hwr/OrtRuntime.cpp). Downloads
# the pinned release of qt/packaging/onnxruntime.env, checks its sha256 and unpacks only what a package carries:
#
#   qt/scripts/onnxruntime-fetch.sh <platform> <folder>
#
#   platform        downloads                                   <folder>/lib/ gets
#   linux-x64       onnxruntime-linux-x64-<ver>.tgz (GitHub)     libonnxruntime.so.<ver>, libonnxruntime.so.1 (a link)
#   win-x64         onnxruntime-win-x64-<ver>.zip (GitHub)       onnxruntime.dll
#   osx-arm64       onnxruntime-osx-arm64-<ver>.tgz (GitHub)     libonnxruntime.1.dylib (the library itself, no link)
#   android-arm64   onnxruntime-android-<ver>.aar (Maven Central) libonnxruntime.so (arm64-v8a)
#
# and every platform <folder>/LICENSE (MIT), <folder>/ThirdPartyNotices.txt and <folder>/VERSION. Not taken: the
# headers, the import libraries, the debug symbols and onnxruntime_providers_shared (only GPU and other execution
# providers load it; the app runs on the CPU).
#
# The downloads are kept in XQT_DOWNLOAD_CACHE when it is set (a folder; a file already there is checked and used,
# which is also how to build without a network: put the files there). sha256sum or shasum, curl, tar and unzip are
# needed (MSYS2: pacman -S unzip).
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <linux-x64|win-x64|osx-arm64|android-arm64> <folder>" >&2
    exit 2
fi
platform=$1
dest=$2
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../packaging/onnxruntime.env
source "$here/../packaging/onnxruntime.env"
ver=$XQT_ORT_VERSION
github="https://github.com/microsoft/onnxruntime/releases/download/v$ver"
raw="https://raw.githubusercontent.com/microsoft/onnxruntime/v$ver"
maven="https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/$ver"

case "$platform" in
    linux-x64) file="onnxruntime-linux-x64-$ver.tgz" url="$github/$file" sha=$XQT_ORT_LINUX_X64_SHA256 ;;
    win-x64) file="onnxruntime-win-x64-$ver.zip" url="$github/$file" sha=$XQT_ORT_WIN_X64_SHA256 ;;
    osx-arm64) file="onnxruntime-osx-arm64-$ver.tgz" url="$github/$file" sha=$XQT_ORT_OSX_ARM64_SHA256 ;;
    android-arm64) file="onnxruntime-android-$ver.aar" url="$maven/$file" sha=$XQT_ORT_ANDROID_AAR_SHA256 ;;
    *) echo "unknown platform: $platform" >&2; exit 2 ;;
esac

sha256_of() {
    if command -v sha256sum > /dev/null; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cache=${XQT_DOWNLOAD_CACHE:-$work}
mkdir -p "$cache"

# fetch <url> <file name> <sha256>: the checked file in the cache (downloaded when missing or not matching)
fetch() {
    local url=$1 name=$2 want=$3 path="$cache/$2"
    if [[ -f "$path" && "$(sha256_of "$path")" == "$want" ]]; then
        echo "onnxruntime: $name (from $cache)" >&2
    else
        echo "onnxruntime: downloading $url" >&2
        curl -fsSL --retry 3 --retry-delay 5 --connect-timeout 30 -o "$path.part" "$url"
        mv "$path.part" "$path"
        local got
        got=$(sha256_of "$path")
        if [[ "$got" != "$want" ]]; then
            echo "::error::onnxruntime: $name has sha256 $got, qt/packaging/onnxruntime.env says $want" >&2
            rm -f "$path"
            exit 1
        fi
    fi
    echo "$path"
}

archive=$(fetch "$url" "$file" "$sha")
rm -rf "$dest"
mkdir -p "$dest/lib"
unpacked="$work/unpacked"
mkdir -p "$unpacked"

case "$platform" in
    linux-x64)
        top="onnxruntime-linux-x64-$ver"
        tar -xzf "$archive" -C "$unpacked" "$top/lib/libonnxruntime.so.$ver" "$top/LICENSE" "$top/ThirdPartyNotices.txt"
        cp "$unpacked/$top/lib/libonnxruntime.so.$ver" "$dest/lib/"
        ln -s "libonnxruntime.so.$ver" "$dest/lib/libonnxruntime.so.1"
        cp "$unpacked/$top/LICENSE" "$unpacked/$top/ThirdPartyNotices.txt" "$dest/"
        ;;
    win-x64)
        top="onnxruntime-win-x64-$ver"
        unzip -q "$archive" "$top/lib/onnxruntime.dll" "$top/LICENSE" "$top/ThirdPartyNotices.txt" -d "$unpacked"
        cp "$unpacked/$top/lib/onnxruntime.dll" "$dest/lib/"
        cp "$unpacked/$top/LICENSE" "$unpacked/$top/ThirdPartyNotices.txt" "$dest/"
        ;;
    osx-arm64)
        top="onnxruntime-osx-arm64-$ver"
        # (the archive's paths start with ./)
        tar -xzf "$archive" -C "$unpacked"
        top="$unpacked/$top"
        # The library under the name the app opens (its install name is @rpath/libonnxruntime.1.dylib already)
        cp "$top/lib/libonnxruntime.$ver.dylib" "$dest/lib/libonnxruntime.1.dylib"
        cp "$top/LICENSE" "$top/ThirdPartyNotices.txt" "$dest/"
        ;;
    android-arm64)
        unzip -q "$archive" "jni/arm64-v8a/libonnxruntime.so" -d "$unpacked"
        cp "$unpacked/jni/arm64-v8a/libonnxruntime.so" "$dest/lib/"
        cp "$(fetch "$raw/LICENSE" "onnxruntime-$ver-LICENSE" "$XQT_ORT_LICENSE_SHA256")" "$dest/LICENSE"
        cp "$(fetch "$raw/ThirdPartyNotices.txt" "onnxruntime-$ver-ThirdPartyNotices.txt" "$XQT_ORT_NOTICES_SHA256")" \
            "$dest/ThirdPartyNotices.txt"
        ;;
esac
chmod 0644 "$dest/LICENSE" "$dest/ThirdPartyNotices.txt"
echo "$ver" > "$dest/VERSION"
echo "onnxruntime $ver for $platform in $dest:" >&2
ls -l "$dest" "$dest/lib" >&2
