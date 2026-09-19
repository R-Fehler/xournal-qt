#!/usr/bin/env bash
# Golden tests: xournal-qt-cli must behave exactly like upstream `xournalpp` on the fixture files.
#
#   1. PNG export (--create-img) at several DPIs: pixel identical to upstream
#   2. PDF export (--create-pdf, default backend): rasterized pages pixel identical to upstream
#   3. Round trip (--resave): same structure (--dump) and re-render identical up to GOLDEN_RT_TOLERANCE
#      (re-serialisation rounds coordinates to 8 significant digits, exactly like upstream's save)
#
# The fixtures are copied to the output directory first, so the tests never modify the source tree.
#
# Modes (GOLDEN_MODE):
#   quick (default)  a few representative fixtures (strokes, text, images, PDF background, layers) at 72 dpi.
#                    Meant for routine runs; takes a few seconds.
#   full             every fixture in FIXTURES at GOLDEN_DPIS, including the big multi-page files. Takes minutes;
#                    run it for upstream merges and milestone sign-off (ctest -C Full -L golden-full).
#
# Environment:
#   QT_CLI              path to xournal-qt-cli                     (required)
#   IMGDIFF             path to xoj-imgdiff                        (required)
#   XOJ_UPSTREAM_BIN    upstream xournalpp built at the fork's merge base (default: ../xournalpp/build/xournalpp)
#   FIXTURES            directory with .xopp/.xoj files            (default: <repo>/test/files)
#   GOLDEN_OUT          output directory                           (default: ./golden-out)
#   GOLDEN_MODE         quick | full                               (default: quick)
#   GOLDEN_DPIS         DPIs for the PNG comparison                (default: quick "72", full "72 150")
#   GOLDEN_TOLERANCE    per-channel tolerance vs upstream          (default: 0)
#   GOLDEN_RT_TOLERANCE per-channel tolerance for the round trip   (default: 1)
#   GOLDEN_JOBS         parallel jobs                              (default: nproc)
# Exit code: 0 all passed, 1 failures, 77 skipped (no upstream binary).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
QT_CLI="${QT_CLI:?set QT_CLI}"
IMGDIFF="${IMGDIFF:?set IMGDIFF}"
UP_BIN="${XOJ_UPSTREAM_BIN:-$ROOT/../xournalpp/build/xournalpp}"
FIXTURES_SRC="${FIXTURES:-$ROOT/test/files}"
OUT="${GOLDEN_OUT:-$PWD/golden-out}"
MODE="${GOLDEN_MODE:-quick}"
if [[ "$MODE" == full ]]; then
    DPIS="${GOLDEN_DPIS:-72 150}"
else
    DPIS="${GOLDEN_DPIS:-72}"
fi
# Representative small fixtures for the quick mode (paths relative to FIXTURES).
QUICK_FIXTURES=(
    test1.xoj                              # strokes (xoj format)
    load/strokes.xopp                      # pressure strokes, line styles
    load/text-fileversion-5.xopp           # text (pango)
    load/image-fileversion-5.xopp          # images
    load/layers.xopp                       # layers
    packaged_xopp/pdfBackground/new.xopp   # attached PDF background (zip container)
)
TOL="${GOLDEN_TOLERANCE:-0}"
RT_TOL="${GOLDEN_RT_TOLERANCE:-1}"
JOBS="${GOLDEN_JOBS:-$(nproc)}"

if [[ ! -x "$UP_BIN" ]]; then
    echo "SKIP: upstream xournalpp binary not found at $UP_BIN (set XOJ_UPSTREAM_BIN)"
    exit 77
fi
HAVE_PDFTOPPM=1
command -v pdftoppm >/dev/null || { HAVE_PDFTOPPM=0; echo "note: pdftoppm not found, PDF comparison disabled"; }

rm -rf "$OUT"
mkdir -p "$OUT/cases" "$OUT/fixtures"
FIXTURES="$OUT/fixtures"
if [[ "$MODE" == full ]]; then
    cp -a "$FIXTURES_SRC"/. "$FIXTURES"
else
    for rel in "${QUICK_FIXTURES[@]}"; do
        mkdir -p "$FIXTURES/$(dirname "$rel")"
        # Copy the fixture's whole directory: it may reference neighbouring files (PDF/image backgrounds).
        cp -a "$FIXTURES_SRC/$(dirname "$rel")"/. "$FIXTURES/$(dirname "$rel")/" 2>/dev/null
    done
fi

# Appends "ok|fail<TAB>description" to the case's result file.
record() { printf '%s\t%s\n' "$1" "$2" >>"$RESULTS"; }

compare_dirs() {  # compare_dirs <label> <actualDir> <expectedDir> <tolerance>
    local label="$1" a="$2" b="$3" tol="$4"
    local la lb f res ok=1
    la="$(cd "$a" && ls | sort | tr '\n' ' ')"
    lb="$(cd "$b" && ls | sort | tr '\n' ' ')"
    if [[ "$la" != "$lb" ]]; then
        record fail "$label: different output files: [$la] vs [$lb]"
        return
    fi
    [[ -z "$la" ]] && return
    for f in $la; do
        if ! res="$("$IMGDIFF" "$a/$f" "$b/$f" --tolerance "$tol" --diff "$a/$f.diff.png")"; then
            ok=0
            record fail "$label/$f: $res (diff image: $a/$f.diff.png)"
        else
            rm -f "$a/$f.diff.png"
        fi
    done
    [[ $ok == 1 ]] && record ok "$label"
}

run_case() {
    local f="$1"
    local rel="${f#"$FIXTURES"/}"
    local case_dir="$OUT/cases/$(echo "$rel" | tr '/ ' '__')"
    mkdir -p "$case_dir"
    RESULTS="$case_dir/results.tsv"
    : >"$RESULTS"
    local dpi up qt rc_up rc_qt rt

    # 1. PNG export
    for dpi in $DPIS; do
        up="$case_dir/png$dpi-upstream" qt="$case_dir/png$dpi-qt"
        mkdir -p "$up" "$qt"
        "$UP_BIN" "$f" --create-img="$up/page.png" --export-png-dpi="$dpi" >"$up.log" 2>&1
        rc_up=$?
        "$QT_CLI" "$f" --create-img="$qt/page.png" --export-png-dpi="$dpi" >"$qt.log" 2>&1
        rc_qt=$?
        if [[ $rc_up -ne $rc_qt ]]; then
            record fail "$rel png@$dpi: exit code upstream=$rc_up qt=$rc_qt (logs in $case_dir)"
            continue
        fi
        compare_dirs "$rel png@$dpi" "$qt" "$up" "$TOL"
    done

    # 2. PDF export (rasterized at 72 dpi)
    if [[ $HAVE_PDFTOPPM == 1 ]]; then
        up="$case_dir/pdf-upstream" qt="$case_dir/pdf-qt"
        mkdir -p "$up" "$qt"
        "$UP_BIN" "$f" --create-pdf="$up/out.pdf" >"$up.log" 2>&1
        rc_up=$?
        "$QT_CLI" "$f" --create-pdf="$qt/out.pdf" >"$qt.log" 2>&1
        rc_qt=$?
        if [[ $rc_up -ne $rc_qt ]]; then
            record fail "$rel pdf: exit code upstream=$rc_up qt=$rc_qt (logs in $case_dir)"
        elif [[ -f "$up/out.pdf" && -f "$qt/out.pdf" ]]; then
            pdftoppm -r 72 -png "$up/out.pdf" "$up/p" && rm "$up/out.pdf"
            pdftoppm -r 72 -png "$qt/out.pdf" "$qt/p" && rm "$qt/out.pdf"
            compare_dirs "$rel pdf" "$qt" "$up" "$TOL"
        fi
    fi

    # 3. Round trip through the fork's save path
    rt="$case_dir/roundtrip"
    mkdir -p "$rt"
    if "$QT_CLI" "$f" --dump >"$rt/original.dump" 2>/dev/null; then
        if "$QT_CLI" "$f" --resave="$rt/resaved.xopp" >"$rt/resave.log" 2>&1 &&
            "$QT_CLI" "$rt/resaved.xopp" --dump >"$rt/resaved.dump" 2>&1; then
            if diff -q "$rt/original.dump" "$rt/resaved.dump" >/dev/null; then
                mkdir -p "$rt/a" "$rt/b"
                "$QT_CLI" "$f" --create-img="$rt/a/page.png" --export-png-dpi=72 >/dev/null 2>&1
                "$QT_CLI" "$rt/resaved.xopp" --create-img="$rt/b/page.png" --export-png-dpi=72 >/dev/null 2>&1
                compare_dirs "$rel roundtrip" "$rt/b" "$rt/a" "$RT_TOL"
            else
                record fail "$rel roundtrip: structure differs (diff $rt/original.dump $rt/resaved.dump)"
            fi
        else
            record fail "$rel roundtrip: resave or reload failed (see $rt/resave.log)"
        fi
    fi
}

if [[ "$MODE" == full ]]; then
    mapfile -d '' files < <(find "$FIXTURES" -type f \( -name '*.xopp' -o -name '*.xoj' \) -print0 | sort -z)
else
    files=()
    for rel in "${QUICK_FIXTURES[@]}"; do
        files+=("$FIXTURES/$rel")
    done
fi
echo "Golden tests ($MODE): ${#files[@]} fixtures, DPIs [$DPIS], $JOBS jobs, upstream: $UP_BIN"

for f in "${files[@]}"; do
    while [[ $(jobs -rp | wc -l) -ge $JOBS ]]; do
        wait -n
    done
    run_case "$f" &
done
wait

pass=$(cat "$OUT"/cases/*/results.tsv | grep -c '^ok' || true)
fail=$(cat "$OUT"/cases/*/results.tsv | grep -c '^fail' || true)
echo "Golden tests: $pass passed, $fail failed. Output: $OUT"
if [[ $fail -gt 0 ]]; then
    grep -h '^fail' "$OUT"/cases/*/results.tsv | cut -f2- | sed 's/^/  FAIL: /'
    exit 1
fi
exit 0
