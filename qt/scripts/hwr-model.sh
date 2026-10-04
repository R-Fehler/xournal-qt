#!/usr/bin/env bash
# xournal-qt: put the handwriting search's model where the app looks for it (qt/docs/handwriting-search.md).
#
# The model is TrOCR-small handwritten, int8, as Xenova's ONNX export on Hugging Face (Xenova/trocr-small-handwritten,
# MIT; about 64 MB). This script copies it from the Hugging Face cache (where the research trials put it,
# qt/research/hwr) or downloads it, and writes the manifest the app reads ("model.json": the files with their sha256
# and sizes, the decoder's start and end tokens). For development and for the author's machines; the app itself
# downloads the model from Settings (once the sha256s are pinned in qt/src/hwr/ModelDownload.cpp).
#
#   qt/scripts/hwr-model.sh                  # into ~/.local/share/xournal-qt/models/trocr-small-hw-int8/
#   qt/scripts/hwr-model.sh /some/folder     # elsewhere (then: XQT_HWR_MODEL=/some/folder, or the setting)
#   XQT_HWR_REVISION=<commit> qt/scripts/hwr-model.sh   # another revision of the repository
#   XQT_HWR_DOWNLOAD=1 qt/scripts/hwr-model.sh          # download even when the cache has it
#
# It prints the sha256 of each file and the revision: the lines to pin in qt/src/hwr/ModelDownload.cpp.
# Needs: bash, coreutils (sha256sum, stat), curl (to download), python3 or jq (to read generation_config.json; else
# the defaults 2/2 are used).
set -euo pipefail

REPO="Xenova/trocr-small-handwritten"
# The revision the app was tested with. Pin it to a commit hash (the script prints the one it got): "main" follows the
# repository, whose files may change.
REVISION="${XQT_HWR_REVISION:-main}"
NAME="trocr-small-hw-int8"
FILES=(
    "onnx/encoder_model_quantized.onnx"
    "onnx/decoder_model_merged_quantized.onnx"
    "tokenizer.json"
    "tokenizer_config.json"
    "config.json"
    "generation_config.json"
    "preprocessor_config.json"
)
NEEDED=("onnx/encoder_model_quantized.onnx" "onnx/decoder_model_merged_quantized.onnx" "tokenizer.json")

DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
TARGET="${1:-$DATA/xournal-qt/models/$NAME}"
HF_HOME_DIR="${HF_HOME:-$HOME/.cache/huggingface}"
CACHE="$HF_HOME_DIR/hub/models--${REPO//\//--}"

die() { echo "hwr-model.sh: $*" >&2; exit 1; }

work="$(mktemp -d "${TMPDIR:-/tmp}/xqt-hwr-model.XXXXXX")"
trap 'rm -rf "$work"' EXIT

source_dir=""
revision_used=""
# 1. The Hugging Face cache: the snapshot of the revision (or, for "main", the one refs/main names)
if [ -z "${XQT_HWR_DOWNLOAD:-}" ] && [ -d "$CACHE/snapshots" ]; then
    snap=""
    if [ -d "$CACHE/snapshots/$REVISION" ]; then
        snap="$REVISION"
    elif [ -f "$CACHE/refs/$REVISION" ]; then
        snap="$(cat "$CACHE/refs/$REVISION")"
    fi
    if [ -n "$snap" ] && [ -d "$CACHE/snapshots/$snap" ]; then
        ok=1
        for f in "${NEEDED[@]}"; do
            [ -s "$CACHE/snapshots/$snap/$f" ] || ok=0
        done
        if [ "$ok" = 1 ]; then
            source_dir="$CACHE/snapshots/$snap"
            revision_used="$snap"
            echo "Copying from the Hugging Face cache: $source_dir"
        fi
    fi
fi

# 2. Else download the revision (resolve URLs; curl follows the redirect to the CDN)
if [ -z "$source_dir" ]; then
    command -v curl >/dev/null || die "curl is needed to download the model"
    base="https://huggingface.co/$REPO/resolve/$REVISION"
    echo "Downloading $REPO at $REVISION from huggingface.co (about 64 MB) ..."
    for f in "${FILES[@]}"; do
        mkdir -p "$work/files/$(dirname "$f")"
        headers="$work/headers"
        if ! curl -fL --retry 3 --retry-delay 2 -C - -D "$headers" -o "$work/files/$f" "$base/$f"; then
            for n in "${NEEDED[@]}"; do
                [ "$n" = "$f" ] && die "could not download $f"
            done
            echo "  ($f is not there: skipped)"
            rm -f "$work/files/$f"
            continue
        fi
        commit="$(grep -i '^x-repo-commit:' "$headers" | tail -1 | tr -d '\r' | awk '{print $2}')"
        if [ -n "$commit" ]; then
            if [ -n "$revision_used" ] && [ "$revision_used" != "$commit" ]; then
                die "the repository changed while downloading ($revision_used, then $commit): run it again"
            fi
            revision_used="$commit"
        fi
        echo "  $f"
    done
    source_dir="$work/files"
    [ -n "$revision_used" ] || revision_used="$REVISION"
fi

# 3. Into a new folder next to the target, then in its place (the app never sees half a model)
parent="$(dirname "$TARGET")"
mkdir -p "$parent"
staging="$(mktemp -d "$parent/.$NAME.XXXXXX")"
chmod 755 "$staging"
trap 'rm -rf "$work" "$staging"' EXIT
for f in "${FILES[@]}"; do
    if [ -f "$source_dir/$f" ]; then
        mkdir -p "$staging/$(dirname "$f")"
        cp -L "$source_dir/$f" "$staging/$f"
    fi
done

token() {  # token <key> <default>: from generation_config.json
    local file="$staging/generation_config.json" v=""
    if [ -f "$file" ]; then
        if command -v python3 >/dev/null; then
            v="$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get(sys.argv[2], ''))" "$file" "$1" 2>/dev/null || true)"
        elif command -v jq >/dev/null; then
            v="$(jq -r ".$1 // empty" "$file" 2>/dev/null || true)"
        fi
    fi
    echo "${v:-$2}"
}
start="$(token decoder_start_token_id 2)"
eos="$(token eos_token_id 2)"

{
    echo "{"
    echo "  \"name\": \"$NAME\","
    echo "  \"source\": \"https://huggingface.co/$REPO\","
    echo "  \"revision\": \"$revision_used\","
    echo "  \"license\": \"MIT\","
    echo "  \"encoder\": \"onnx/encoder_model_quantized.onnx\","
    echo "  \"decoder\": \"onnx/decoder_model_merged_quantized.onnx\","
    echo "  \"tokenizer\": \"tokenizer.json\","
    echo "  \"decoder_start_token_id\": $start,"
    echo "  \"eos_token_id\": $eos,"
    echo "  \"image_size\": 384,"
    echo "  \"files\": {"
    first=1
    for f in "${FILES[@]}"; do
        [ -f "$staging/$f" ] || continue
        sum="$(sha256sum "$staging/$f" | awk '{print $1}')"
        size="$(stat -c %s "$staging/$f")"
        [ "$first" = 1 ] || echo ","
        first=0
        printf '    "%s": {"sha256": "%s", "size": %s}' "$f" "$sum" "$size"
    done
    echo
    echo "  }"
    echo "}"
} > "$staging/model.json"

if [ -d "$TARGET" ]; then
    rm -rf "$TARGET.old"
    mv "$TARGET" "$TARGET.old"
fi
mv "$staging" "$TARGET"
rm -rf "$TARGET.old"
trap 'rm -rf "$work"' EXIT

echo
echo "The model is in $TARGET (revision $revision_used)."
echo "To pin it in qt/src/hwr/ModelDownload.cpp:"
echo "  revision $revision_used"
for f in "${FILES[@]}"; do
    [ -f "$TARGET/$f" ] || continue
    echo "  $(sha256sum "$TARGET/$f" | awk '{print $1}')  $(stat -c %s "$TARGET/$f")  $f"
done
if [ "$TARGET" != "$DATA/xournal-qt/models/$NAME" ]; then
    echo "Run the app with XQT_HWR_MODEL=$TARGET (or choose the folder in Settings)."
fi
