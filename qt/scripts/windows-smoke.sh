#!/usr/bin/env bash
# Smoke test of the portable Windows folder, in MSYS2 (the CI runs it after windows-deploy.sh, see qt/docs/windows.md):
#
#   qt/scripts/windows-smoke.sh <program folder> <output folder>
#
# The CLI and the app run with a PATH of Windows alone: a DLL missing from the folder fails here, not on the user's
# machine. Everything they print goes to <output folder>/<step>.log, with the files they wrote next to it.
#   1. xournal-qt-cli --version
#   2. PNG export of a document with text (Pango, fonts) and PDF export of one with images
#   3. PDF export of a document with a PDF background (poppler, qpdf)
#   4. the app off-screen: opens a library and a document, saves a screenshot of its window after 5 s, quits
set -uo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <program folder> <output folder>" >&2
    exit 2
fi
dist=$(cd "$1" && pwd)
mkdir -p "$2"
out=$(cd "$2" && pwd)
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
fixtures="$source_dir/test/files"
bin="$dist/bin"
windows=$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")
clean_path="$windows/System32:$windows:$windows/System32/Wbem"
# Arguments are given as Windows paths (cygpath -m); MSYS2 must not rewrite them.
export MSYS2_ARG_CONV_EXCL='*'
win() { cygpath -m "$1"; }

failures=0
# run <step> <seconds> <program> <arguments...>
run() {
    local step=$1 seconds=$2
    shift 2
    printf '\n=== %s: %s\n' "$step" "$*"
    local rc=0
    PATH="$clean_path" /usr/bin/timeout --kill-after=10 "$seconds" "$@" > "$out/$step.log" 2>&1 || rc=$?
    cat "$out/$step.log"
    if ((rc == 124 || rc == 137)); then
        echo "::error::$step: still running after $seconds s (killed)"
    elif ((rc != 0)); then
        echo "::error::$step: exit code $rc (see $step.log)"
    else
        echo "=== $step: ok"
    fi
    return $rc
}
# expect_file <step> <file>: the step wrote a file that is not empty
expect_file() {
    if [[ -s "$2" ]]; then
        echo "=== $1: wrote $(basename "$2") ($(stat -c %s "$2") bytes)"
    else
        echo "::error::$1: $(basename "$2") was not written"
        failures=$((failures + 1))
    fi
}

library="$out/library"
mkdir -p "$library"
cp "$fixtures/load/text-fileversion-5.xopp" "$fixtures/load/image-fileversion-5.xopp" "$library/"
cp "$fixtures/packaged_xopp/pdfBackground/old.xopp" "$fixtures/packaged_xopp/pdfBackground/old.xopp.bg.pdf" "$library/"

# Settings and caches of these runs stay in the output folder.
export XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME
XDG_CONFIG_HOME=$(win "$out/config")
XDG_CACHE_HOME=$(win "$out/cache")
XDG_DATA_HOME=$(win "$out/data")

cli="$bin/xournal-qt-cli.exe"
run cli-version 60 "$cli" --version || failures=$((failures + 1))
run cli-text-png 120 "$cli" "$(win "$library/text-fileversion-5.xopp")" \
    "--create-img=$(win "$out/text.png")" --export-png-dpi=72 || failures=$((failures + 1))
ls "$out"/text*.png > /dev/null 2>&1 || { echo "::error::cli-text-png: no PNG written"; failures=$((failures + 1)); }
run cli-image-pdf 120 "$cli" "$(win "$library/image-fileversion-5.xopp")" \
    "--create-pdf=$(win "$out/image.pdf")" || failures=$((failures + 1))
expect_file cli-image-pdf "$out/image.pdf"
run cli-pdf-background 120 "$cli" "$(win "$library/old.xopp")" \
    "--create-pdf=$(win "$out/old.pdf")" || failures=$((failures + 1))
expect_file cli-pdf-background "$out/old.pdf"

# The app: off-screen, Qt Quick's software renderer (no GPU on the runner), log to stderr (a GUI program's messages
# go to the debugger otherwise).
app_env=(QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_FORCE_STDERR_LOGGING=1
    "XQT_SCREENSHOT=$(win "$out/app.png")" XQT_SCREENSHOT_DELAY_MS=5000)
if ! run app 180 /usr/bin/env "${app_env[@]}" "$bin/xournal-qt.exe" "$(win "$library")" \
    "$(win "$library/text-fileversion-5.xopp")"; then
    failures=$((failures + 1))
    # Again, with Qt saying which plugins it looks for and why they do not load.
    run app-plugins 180 /usr/bin/env "${app_env[@]}" QT_DEBUG_PLUGINS=1 "QML_IMPORT_TRACE=1" "$bin/xournal-qt.exe" \
        "$(win "$library")" || true
fi
expect_file app "$out/app.png"

printf '\n=== %d failure(s)\n' "$failures"
((failures == 0))
