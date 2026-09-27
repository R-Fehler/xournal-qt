#!/usr/bin/env bash
# Smoke test of the macOS app bundle (the CI runs it after macos-deploy.sh, see qt/docs/macos.md):
#
#   qt/scripts/macos-smoke.sh <xournal-qt.app> <output folder>
#
# Everything runs from the bundle, as a user starts it. What each step prints goes to <output folder>/<step>.log, with
# the files it wrote next to it.
#   1. xournal-qt-cli --version
#   2. exports that tell apart what fails: strokes to PNG (raster, no text), text to PDF (text, no raster), text to
#      PNG (both), images to PDF (gdk-pixbuf), a PDF background to PDF (poppler, qpdf)
#   3. the app off-screen: opens a library and a document, saves a screenshot of its window after 5 s, quits
#
# The CI hides Homebrew (/opt/homebrew) while this runs, so that a library missing from the bundle fails here and not
# on a Mac without Homebrew.
# A step that fails (not one that hangs) runs again under lldb, which prints the backtraces of every thread.
set -uo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <xournal-qt.app> <output folder>" >&2
    exit 2
fi
app=$(cd "$1" && pwd)
mkdir -p "$2"
out=$(cd "$2" && pwd)
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
fixtures="$source_dir/test/files"
macos="$app/Contents/MacOS"

failures=0
lldb_runs=0
max_lldb_runs=6

# run_limited <seconds> <log> [VAR=value ...] <program> <arguments...>: runs it with the variables set, output into
# the log; exit code 124 when the time ran out. Plain bash, no timeout(1) (macOS has none) and no /usr/bin/env or perl
# in between (macOS drops DYLD_* variables when it starts a system program: DYLD_PRINT_LIBRARIES=1 reaches the app).
run_limited() {
    local seconds=$1 log=$2
    shift 2
    local envs=()
    while [[ $# -gt 0 && $1 =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; do
        envs+=("$1")
        shift
    done
    rm -f "$log.timeout"
    (
        for e in ${envs[@]+"${envs[@]}"}; do
            export "${e?}"
        done
        exec "$@"
    ) > "$log" 2>&1 &
    local pid=$!
    (
        for ((i = 0; i < seconds; i++)); do
            sleep 1
            kill -0 "$pid" 2> /dev/null || exit 0
        done
        touch "$log.timeout"
        kill -9 "$pid" 2> /dev/null
    ) &
    local watchdog=$!
    local rc=0
    wait "$pid" || rc=$?
    wait "$watchdog" 2> /dev/null
    if [[ -e "$log.timeout" ]]; then
        rm -f "$log.timeout"
        return 124
    fi
    return $rc
}

# debug <step> [VAR=value ...] <program> <arguments...>: the same again under lldb
debug() {
    local step=$1
    shift
    local envs=()
    while [[ $# -gt 0 && $1 =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; do
        envs+=("$1")
        shift
    done
    if ((lldb_runs >= max_lldb_runs)); then
        echo "=== $step: no lldb run (already $lldb_runs)"
        return
    fi
    lldb_runs=$((lldb_runs + 1))
    printf '\n=== %s under lldb\n' "$step"
    run_limited 300 "$out/$step.lldb.log" ${envs[@]+"${envs[@]}"} lldb --batch -o run -k 'thread backtrace all' \
        -k 'image list' -k quit -- "$@"
    # (the backtraces, not the list of the hundreds of images loaded)
    sed -n '1,/image list/p' "$out/$step.lldb.log" | tail -n 400
}

# attempt <step> <seconds> [VAR=value ...] <program> <arguments...>: runs it; on failure once more under lldb.
attempt() {
    local step=$1 seconds=$2
    shift 2
    printf '\n=== %s: %s\n' "$step" "$*"
    local rc=0
    run_limited "$seconds" "$out/$step.log" "$@" || rc=$?
    tail -n 200 "$out/$step.log"
    if ((rc == 124)); then
        echo "::error::$step: still running after $seconds s (killed)"
    elif ((rc != 0)); then
        echo "::error::$step: exit code $rc (see $step.log)"
        debug "$step" "$@"
    else
        echo "=== $step: ok"
    fi
    return $rc
}

# expect_file <step> <file>: the step wrote a file that is not empty
expect_file() {
    if [[ -s "$2" ]]; then
        echo "=== $1: wrote $(basename "$2") ($(wc -c < "$2" | tr -d ' ') bytes)"
    else
        echo "::error::$1: $(basename "$2") was not written"
        failures=$((failures + 1))
    fi
}

library="$out/library"
mkdir -p "$library"
cp "$fixtures/load/text-fileversion-5.xopp" "$fixtures/load/image-fileversion-5.xopp" "$fixtures/load/strokes.xopp" \
    "$library/"
cp "$fixtures/packaged_xopp/pdfBackground/old.xopp" "$fixtures/packaged_xopp/pdfBackground/old.xopp.bg.pdf" "$library/"

# Settings, caches and the documents folder of these runs stay in the output folder.
export HOME="$out/home" XDG_CONFIG_HOME="$out/config" XDG_CACHE_HOME="$out/cache" XDG_DATA_HOME="$out/data"
mkdir -p "$HOME/Documents"

# --- The CLI -------------------------------------------------------------------------------------------------------
cli="$macos/xournal-qt-cli"
text="$library/text-fileversion-5.xopp"
attempt cli-version 60 "$cli" --version || failures=$((failures + 1))
attempt cli-strokes-png 120 "$cli" "$library/strokes.xopp" "--create-img=$out/strokes.png" --export-png-dpi=72 ||
    failures=$((failures + 1))
ls "$out"/strokes*.png > /dev/null 2>&1 || { echo "::error::cli-strokes-png: no PNG written"; failures=$((failures + 1)); }
attempt cli-text-pdf 120 "$cli" "$text" "--create-pdf=$out/text.pdf" || failures=$((failures + 1))
expect_file cli-text-pdf "$out/text.pdf"
attempt cli-text-png 300 "$cli" "$text" "--create-img=$out/text.png" --export-png-dpi=72 || failures=$((failures + 1))
ls "$out"/text*.png > /dev/null 2>&1 || { echo "::error::cli-text-png: no PNG written"; failures=$((failures + 1)); }
attempt cli-image-pdf 120 "$cli" "$library/image-fileversion-5.xopp" "--create-pdf=$out/image.pdf" ||
    failures=$((failures + 1))
expect_file cli-image-pdf "$out/image.pdf"
attempt cli-pdf-background 120 "$cli" "$library/old.xopp" "--create-pdf=$out/old.pdf" || failures=$((failures + 1))
expect_file cli-pdf-background "$out/old.pdf"

# --- The app -------------------------------------------------------------------------------------------------------
# Off-screen, Qt Quick's software renderer.
app_env=(QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software XQT_SCREENSHOT_DELAY_MS=5000)
if ! attempt app 300 "${app_env[@]}" "XQT_SCREENSHOT=$out/app.png" "$macos/xournal-qt" \
    "$library" "$text"; then
    failures=$((failures + 1))
    # Again, with Qt saying which plugins it looks for and why they do not load.
    printf '\n=== app-plugins (diagnostic)\n'
    run_limited 180 "$out/app-plugins.log" "${app_env[@]}" "XQT_SCREENSHOT=$out/app-plugins.png" QT_DEBUG_PLUGINS=1 \
        QML_IMPORT_TRACE=1 "$macos/xournal-qt" "$library" "$text"
    echo "exit code $?"
    tail -n 300 "$out/app-plugins.log"
fi
expect_file app "$out/app.png"

printf '\n=== %d failure(s)\n' "$failures"
((failures == 0))
