#!/usr/bin/env bash
# Smoke test of the portable Windows folder, in MSYS2 (the CI runs it after windows-deploy.sh, see qt/docs/windows.md):
#
#   qt/scripts/windows-smoke.sh <program folder> <output folder>
#
# The CLI and the app run with a PATH of Windows alone: a DLL missing from the folder fails here, not on the user's
# machine. Everything they print goes to <output folder>/<step>.log, with the files they wrote next to it.
#   1. xournal-qt-cli --version
#   2. exports that tell apart what fails: strokes to PNG (raster, no text), text to PDF (text, no raster), text to
#      PNG (both), images to PDF, a PDF background to PDF (poppler, qpdf)
#   3. the app off-screen: opens a library and a document, saves a screenshot of its window after 5 s, quits
#   4. text-probe (qt/tools/text-probe.c): Pango and Cairo alone, drawing text into a PNG and a PDF, with the DLLs of
#      the folder; with each font backend and with and without the UTF-8 C locale
#
# When a step fails (not when it hangs), it runs again under gdb, which stops at the crash, abort() or exit() and
# prints the backtraces and the loaded DLLs into <step>.gdb.log. A failing text export also runs with FC_DEBUG=1 and
# G_MESSAGES_DEBUG=all, with Pango's fontconfig backend, and without the UTF-8 C locale (XQT_NO_UTF8_LOCALE=1); a
# failing app with Qt's plugin and QML import traces and the same variants. Only the steps of 1-3 count as failures.
#
# Exit codes as MSYS2 reports them for Windows programs: 139 an access violation (SIGSEGV), 127 any other fatal
# NTSTATUS (stack overflow, heap corruption, __fastfail from abort() or an invalid C runtime parameter) or a DLL that
# cannot be loaded, 124 the time ran out.
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
gdb_runs=0
max_gdb_runs=8

# debug <step> [VAR=value ...] <program> <arguments...>: the same again under gdb (with MSYS2's PATH for gdb itself;
# the program still loads its DLLs from its own folder first).
debug() {
    local step=$1
    shift
    local envs=()
    while [[ $# -gt 0 && $1 =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; do
        envs+=("$1")
        shift
    done
    if ! command -v gdb > /dev/null; then
        echo "::warning::no gdb (mingw-w64-ucrt-x86_64-gdb) for a backtrace of $step"
        return
    fi
    if ((gdb_runs >= max_gdb_runs)); then
        echo "=== $step: no gdb run (already $gdb_runs)"
        return
    fi
    gdb_runs=$((gdb_runs + 1))
    printf '\n=== %s under gdb\n' "$step"
    /usr/bin/timeout --kill-after=10 300 /usr/bin/env "${envs[@]}" gdb -q -batch -nx \
        -ex 'set pagination off' -ex 'set width 0' -ex 'set print thread-events off' -ex 'set debuginfod enabled off' \
        -ex 'set breakpoint pending on' -ex 'break abort' -ex 'break exit' -ex 'break _exit' \
        -ex 'run' \
        -ex 'echo \n--- backtrace (innermost 60)\n' -ex 'bt 60' \
        -ex 'echo \n--- backtrace (outermost 15)\n' -ex 'bt -15' \
        -ex 'echo \n--- registers\n' -ex 'info registers rip rsp' \
        -ex 'echo \n--- all threads\n' -ex 'thread apply all bt 15' \
        -ex 'echo \n--- DLLs\n' -ex 'info sharedlibrary' \
        --args "$@" > "$out/$step.gdb.log" 2>&1
    cat "$out/$step.gdb.log"
}

# attempt <step> <seconds> [VAR=value ...] <program> <arguments...>: runs it with a PATH of Windows alone; on
# failure once more under gdb. Returns the exit code.
attempt() {
    local step=$1 seconds=$2
    shift 2
    printf '\n=== %s: %s\n' "$step" "$*"
    local rc=0
    PATH="$clean_path" /usr/bin/timeout --kill-after=10 "$seconds" /usr/bin/env "$@" > "$out/$step.log" 2>&1 || rc=$?
    cat "$out/$step.log"
    if ((rc == 124 || rc == 137)); then
        echo "::error::$step: still running after $seconds s (killed)"
    elif ((rc != 0)); then
        echo "::error::$step: exit code $rc (see $step.log)"
        debug "$step" "$@"
    else
        echo "=== $step: ok"
    fi
    return $rc
}

# variant <step> <seconds> [VAR=value ...] <program> <arguments...>: a diagnostic run, no gdb, not counted.
variant() {
    local step=$1 seconds=$2
    shift 2
    printf '\n=== %s (diagnostic): %s\n' "$step" "$*"
    local rc=0
    PATH="$clean_path" /usr/bin/timeout --kill-after=10 "$seconds" /usr/bin/env "$@" > "$out/$step.log" 2>&1 || rc=$?
    tail -n 200 "$out/$step.log"
    echo "=== $step: exit code $rc"
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
cp "$fixtures/load/text-fileversion-5.xopp" "$fixtures/load/image-fileversion-5.xopp" "$fixtures/load/strokes.xopp" \
    "$library/"
cp "$fixtures/packaged_xopp/pdfBackground/old.xopp" "$fixtures/packaged_xopp/pdfBackground/old.xopp.bg.pdf" "$library/"

# Settings and caches of these runs stay in the output folder.
export XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME
XDG_CONFIG_HOME=$(win "$out/config")
XDG_CACHE_HOME=$(win "$out/cache")
XDG_DATA_HOME=$(win "$out/data")
fontconfig_env=(PANGOCAIRO_BACKEND=fc "FONTCONFIG_PATH=$(win "$dist/etc/fonts")")

# --- The CLI -------------------------------------------------------------------------------------------------------
cli="$bin/xournal-qt-cli.exe"
text="$(win "$library/text-fileversion-5.xopp")"
attempt cli-version 60 "$cli" --version || failures=$((failures + 1))
attempt cli-strokes-png 120 "$cli" "$(win "$library/strokes.xopp")" \
    "--create-img=$(win "$out/strokes.png")" --export-png-dpi=72 || failures=$((failures + 1))
ls "$out"/strokes*.png > /dev/null 2>&1 || { echo "::error::cli-strokes-png: no PNG written"; failures=$((failures + 1)); }
attempt cli-text-pdf 120 "$cli" "$text" "--create-pdf=$(win "$out/text.pdf")" || failures=$((failures + 1))
expect_file cli-text-pdf "$out/text.pdf"
if ! attempt cli-text-png 120 "$cli" "$text" "--create-img=$(win "$out/text.png")" --export-png-dpi=72; then
    failures=$((failures + 1))
    variant cli-text-png-debug 120 FC_DEBUG=1 G_MESSAGES_DEBUG=all "$cli" "$text" \
        "--create-img=$(win "$out/text-debug.png")" --export-png-dpi=72
    variant cli-text-png-fontconfig 300 "${fontconfig_env[@]}" "$cli" "$text" \
        "--create-img=$(win "$out/text-fontconfig.png")" --export-png-dpi=72
    variant cli-text-png-no-utf8 120 XQT_NO_UTF8_LOCALE=1 "$cli" "$text" \
        "--create-img=$(win "$out/text-no-utf8.png")" --export-png-dpi=72
fi
ls "$out"/text*.png > /dev/null 2>&1 || { echo "::error::cli-text-png: no PNG written"; failures=$((failures + 1)); }
attempt cli-image-pdf 120 "$cli" "$(win "$library/image-fileversion-5.xopp")" \
    "--create-pdf=$(win "$out/image.pdf")" || failures=$((failures + 1))
expect_file cli-image-pdf "$out/image.pdf"
attempt cli-pdf-background 120 "$cli" "$(win "$library/old.xopp")" \
    "--create-pdf=$(win "$out/old.pdf")" || failures=$((failures + 1))
expect_file cli-pdf-background "$out/old.pdf"

# --- The app -------------------------------------------------------------------------------------------------------
# Off-screen, Qt Quick's software renderer (no GPU on the runner), log to stderr (a GUI program's messages go to the
# debugger otherwise).
app_env=(QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_FORCE_STDERR_LOGGING=1 XQT_SCREENSHOT_DELAY_MS=5000)
app_args=("$bin/xournal-qt.exe" "$(win "$library")" "$text")
if ! attempt app 180 "${app_env[@]}" "XQT_SCREENSHOT=$(win "$out/app.png")" "${app_args[@]}"; then
    failures=$((failures + 1))
    # Again, with Qt saying which plugins it looks for and why they do not load.
    variant app-plugins 180 "${app_env[@]}" "XQT_SCREENSHOT=$(win "$out/app-plugins.png")" QT_DEBUG_PLUGINS=1 \
        QML_IMPORT_TRACE=1 "${app_args[@]}"
    variant app-fontconfig 300 "${app_env[@]}" "XQT_SCREENSHOT=$(win "$out/app-fontconfig.png")" \
        "${fontconfig_env[@]}" "${app_args[@]}"
    variant app-no-utf8 180 "${app_env[@]}" "XQT_SCREENSHOT=$(win "$out/app-no-utf8.png")" XQT_NO_UTF8_LOCALE=1 \
        "${app_args[@]}"
    # The library alone, no document: is it the document's page (text) or the window itself?
    variant app-no-document 180 "${app_env[@]}" "XQT_SCREENSHOT=$(win "$out/app-no-document.png")" \
        "$bin/xournal-qt.exe" "$(win "$library")"
fi
expect_file app "$out/app.png"

# --- Pango and Cairo alone -----------------------------------------------------------------------------------------
# Built here with MSYS2's compiler, run with the DLLs of the folder (as the app loads them).
probe="$bin/text-probe.exe"
if command -v gcc > /dev/null && pkg-config --exists pangocairo cairo-pdf; then
    printf '\n=== building text-probe\n'
    # shellcheck disable=SC2046  # pkg-config's flags are words
    if gcc -O1 -g "$source_dir/qt/tools/text-probe.c" $(pkg-config --cflags --libs pangocairo cairo-pdf) -o "$probe"; then
        probe_run() {  # probe_run <step> [VAR=value ...] [utf8]
            local step=$1
            shift
            local envs=() args=()
            for a in "$@"; do
                if [[ $a == *=* ]]; then envs+=("$a"); else args+=("$a"); fi
            done
            printf '\n=== %s (diagnostic)\n' "$step"
            local rc=0
            PATH="$bin:$clean_path" /usr/bin/timeout --kill-after=10 300 /usr/bin/env "${envs[@]}" "$probe" \
                "$(win "$out/$step")" "${args[@]}" > "$out/$step.log" 2>&1 || rc=$?
            cat "$out/$step.log"
            echo "=== $step: exit code $rc"
            if ((rc != 0 && rc != 124 && rc != 137)); then
                debug "$step" "${envs[@]}" "$probe" "$(win "$out/$step-gdb")" "${args[@]}"
            fi
        }
        probe_run probe-default
        probe_run probe-default-utf8 utf8
        probe_run probe-win32 PANGOCAIRO_BACKEND=win32
        probe_run probe-fontconfig "${fontconfig_env[@]}"
    else
        echo "::warning::text-probe did not build"
    fi
    rm -f "$probe"  # (the folder has been published already; keep it as it was)
fi

printf '\n=== %d failure(s)\n' "$failures"
((failures == 0))
