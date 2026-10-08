#!/usr/bin/env bash
# Does a package carry what the handwriting search needs? (the CI runs it in every package job, next to
# `xournal-qt --hwr-info`, which reads a sample with what it finds; qt/docs/development/releasing.md)
#
#   qt/scripts/hwr-package-check.sh models <share/xournal-qt folder>   every model of qt/resources/hwr/ is in
#                                                                      <folder>/hwr-models/<name>/, file for file the
#                                                                      same (sha256), LICENCE.md included
#   qt/scripts/hwr-package-check.sh apk <file.apk>                     the APK has lib/arm64-v8a/libonnxruntime.so,
#                                                                      ONNX Runtime's licence files and every model's
#                                                                      files (as assets, or as Qt resources in the app's
#                                                                      library: then by name only)
#
# Model-agnostic: the models are whatever folders qt/resources/hwr/ has. Exit code 1 when something is missing.
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 models <share/xournal-qt folder> | apk <file.apk>" >&2
    exit 2
fi
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
models_src="$here/../resources/hwr"
missing=0
miss() { echo "::error::handwriting: $*"; missing=$((missing + 1)); }

sha256_of() {
    if command -v sha256sum > /dev/null; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

models=()
for d in "$models_src"/*/; do
    [[ -f "$d/model.json" ]] && models+=("$(basename "$d")")
done
if ((${#models[@]} == 0)); then
    echo "no model under qt/resources/hwr: nothing to check"
fi

case "$1" in
    models)
        share=$2
        for m in ${models[@]+"${models[@]}"}; do
            [[ -f "$models_src/$m/LICENCE.md" ]] || miss "qt/resources/hwr/$m has no LICENCE.md"
            while IFS= read -r -d '' f; do
                rel=${f#"$models_src/$m/"}
                got="$share/hwr-models/$m/$rel"
                if [[ ! -f "$got" ]]; then
                    miss "hwr-models/$m/$rel is not in the package ($share)"
                elif [[ "$(sha256_of "$got")" != "$(sha256_of "$f")" ]]; then
                    miss "hwr-models/$m/$rel differs from qt/resources/hwr/$m/$rel"
                else
                    echo "ok: hwr-models/$m/$rel ($(wc -c < "$got" | tr -d ' ') bytes)"
                fi
            done < <(find "$models_src/$m" -type f -print0)
        done
        ;;
    apk)
        apk=$2
        listing=$(unzip -l "$apk")
        has() { grep -qE "[[:space:]]$1\$" <<< "$listing"; }
        if has 'lib/arm64-v8a/libonnxruntime\.so'; then
            echo "ok: $(grep -E '[[:space:]]lib/arm64-v8a/libonnxruntime\.so$' <<< "$listing" | awk '{print $4 " (" $1 " bytes)"}')"
        else
            miss "lib/arm64-v8a/libonnxruntime.so is not in the APK"
        fi
        for f in LICENSE ThirdPartyNotices.txt; do
            has "assets/share/doc/xournal-qt/onnxruntime/$f" && echo "ok: assets/share/doc/xournal-qt/onnxruntime/$f" ||
                miss "assets/share/doc/xournal-qt/onnxruntime/$f is not in the APK"
        done
        # The models: as assets (any folder .../hwr-models/<name>/), or compiled into the app's library as Qt
        # resources (qt/cmake/XqtAndroid.cmake: the share folder travels as :/xqt-share). rcc keeps a resource's
        # names as UTF-16 (big endian), so they can be found in the library, not their contents.
        app_lib=$(grep -oE 'lib/arm64-v8a/libxournal-qt[^[:space:]]*\.so$' <<< "$listing" | head -n 1 || true)
        lib_strings=""
        if [[ -n "$app_lib" ]]; then
            work=$(mktemp -d)
            trap 'rm -rf "$work"' EXIT
            unzip -q -o "$apk" "$app_lib" -d "$work"
            # (every UTF-16BE string of 4+ characters, as text)
            lib_strings=$(LC_ALL=C strings -e b -n 4 "$work/$app_lib" || true)
        else
            echo "::warning::no lib/arm64-v8a/libxournal-qt*.so in the APK"
        fi
        utf16_has() { grep -qF "$1" <<< "$lib_strings"; }
        for m in ${models[@]+"${models[@]}"}; do
            while IFS= read -r -d '' f; do
                rel=${f#"$models_src/$m/"}
                if grep -qE "[[:space:]]assets/(.*/)?hwr-models/$m/$rel\$" <<< "$listing"; then
                    echo "ok: hwr-models/$m/$rel (an asset)"
                elif utf16_has "hwr-models" && utf16_has "$m" && utf16_has "$(basename "$rel")"; then
                    echo "ok: hwr-models/$m/$rel (a Qt resource in $app_lib, found by name)"
                else
                    miss "hwr-models/$m/$rel is neither an asset of the APK nor a resource of the app's library"
                fi
            done < <(find "$models_src/$m" -type f -print0)
        done
        ;;
    *)
        echo "usage: $0 models <share/xournal-qt folder> | apk <file.apk>" >&2
        exit 2
        ;;
esac

if ((missing)); then
    echo "::error::handwriting: $missing thing(s) missing from the package (listed above)"
    exit 1
fi
echo "handwriting: everything there"
