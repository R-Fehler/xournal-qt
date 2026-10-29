#!/usr/bin/env bash
# Tries the Windows installer on a Windows machine, silently (/S), in MSYS2 (the CI runs it after
# windows-installer.sh; qt/docs/development/windows.md, "The installer"):
#
#   qt/scripts/windows-installer-test.sh <setup.exe> <output folder>
#
# It installs for the user who runs it, into the default folder (%LOCALAPPDATA%\Programs\xournal-qt), and writes
# under HKCU: run it on a CI runner or a test account, not on a machine with Xournal Qt installed. Steps:
#   1. install: the files, the registry entries, the Start menu entry; the installed CLI runs
#   2. a .xopp opened through its file type (cmd's start, as a double-click in Explorer) starts the installed app
#   3. while the app runs, the installer changes nothing and gives up (exit code 4)
#   4. an update over an "older" version: a file the old version had is gone, the settings stay, .xopp opened by
#      another program (the choice not to open .xopp, remembered) stays so
#   5. an older version over a newer one is refused (exit code 3)
#   6. the uninstaller removes the files and its registry entries, and keeps the settings and the other program's
#      .xopp
# Every step writes <step>.log into the output folder. Exit code: the number of failed checks.
set -uo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <setup.exe> <output folder>" >&2
    exit 2
fi
setup=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
mkdir -p "$2"
out=$(cd "$2" && pwd)
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
version=$(sed -n 's/^project(xournal-qt VERSION \([0-9.]*\).*/\1/p' "$source_dir/qt/CMakeLists.txt")
windows=$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")
reg="$windows/System32/reg.exe"
cmd="$windows/System32/cmd.exe"
export MSYS2_ARG_CONV_EXCL='*'  # (reg's /v, cmd's /c and Windows paths as they are)

dir=$(cygpath -u "$LOCALAPPDATA")/Programs/xournal-qt
start_menu=$(cygpath -u "$APPDATA")/Microsoft/Windows/Start\ Menu/Programs/Xournal\ Qt.lnk
settings=$(cygpath -u "$LOCALAPPDATA")/xournal-qt
key='HKCU\Software\xournal-qt'
uninstall_key='HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\xournal-qt'
classes='HKCU\Software\Classes'

failures=0
ok() { echo "  ok: $*"; }
fail() { echo "::error::$*"; failures=$((failures + 1)); }
check() {  # check <what> <command...>
    local what=$1
    shift
    if "$@"; then ok "$what"; else fail "$what"; fi
}
same() { [[ "$1" == "$2" ]] || { echo "    got '$1', expected '$2'"; return 1; }; }

# reg_value <key> [<name>]: the data of a value (the key's default without a name); fails when it is missing
reg_value() {
    local args=(query "$1")
    if (($# >= 2)); then args+=(/v "$2"); else args+=(/ve); fi
    local text
    text=$("$reg" "${args[@]}" 2> /dev/null | tr -d '\r') || return 1
    # (reg prints "    <name>    REG_SZ    <data>")
    sed -n 's/^.*    REG_[A-Z_]*//p' <<< "$text" | head -n 1 | sed 's/^    //'
}
reg_has() { "$reg" query "$@" > /dev/null 2>&1; }
reg_set() { "$reg" add "$1" /v "$2" /t "${4:-REG_SZ}" /d "$3" /f > /dev/null; }
reg_set_default() { "$reg" add "$1" /ve /t REG_SZ /d "$2" /f > /dev/null; }

# run_setup <step> [arguments...]: the installer, silently; returns its exit code
run_setup() {
    local step=$1
    shift
    printf '\n=== %s: %s /S %s\n' "$step" "$(basename "$setup")" "$*"
    local rc=0
    /usr/bin/timeout 300 "$setup" /S "$@" > "$out/$step.log" 2>&1 || rc=$?
    echo "  exit code $rc"
    return $rc
}

if [[ -e "$dir" ]] || reg_has "$key"; then
    echo "::error::Xournal Qt is installed for this user already ($dir, $key): not on a machine of yours"
    exit 1
fi

# The app's settings folder with a file in it, which neither an update nor the uninstaller may touch
mkdir -p "$settings"
echo "kept" > "$settings/installer-test-marker"

# --- 1. Install ------------------------------------------------------------------------------------------------------
rc=0
run_setup install || rc=$?
check "the installer succeeds" same "$rc" 0
check "bin/xournal-qt.exe" test -f "$dir/bin/xournal-qt.exe"
check "bin/xournal-qt-cli.exe" test -f "$dir/bin/xournal-qt-cli.exe"
check "share/xournal-qt" test -d "$dir/share/xournal-qt"
check "Uninstall.exe" test -f "$dir/Uninstall.exe"
check "the Start menu entry" test -f "$start_menu"
check "the version in $key" same "$(reg_value "$key" Version)" "$version"
check "the folder in $key" same "$(reg_value "$key" InstallDir)" "$(cygpath -w "$dir")"
check "the uninstall entry's version" same "$(reg_value "$uninstall_key" DisplayVersion)" "$version"
check "the uninstall entry's command" same "$(reg_value "$uninstall_key" UninstallString)" "\"$(cygpath -w "$dir")\\Uninstall.exe\""
check ".xopp opened by Xournal Qt" same "$(reg_value "$classes\\.xopp")" "xournal-qt.xopp"
check ".xoj opened by Xournal Qt" same "$(reg_value "$classes\\.xoj")" "xournal-qt.xoj"
check ".pdf offers Xournal Qt (Open with)" reg_has "$classes\\.pdf\\OpenWithProgids" /v xournal-qt.pdf
check ".pdf's default not taken" test "$(reg_value "$classes\\.pdf" 2> /dev/null)" != "xournal-qt.pdf"
check "the PDF file type's command" same "$(reg_value "$classes\\xournal-qt.pdf\\shell\\open\\command")" \
    "\"$(cygpath -w "$dir")\\bin\\xournal-qt.exe\" \"%1\""
check "listed in Default apps" same "$(reg_value 'HKCU\Software\RegisteredApplications' xournal-qt)" \
    'Software\xournal-qt\Capabilities'
check "the capabilities name .pdf" same "$(reg_value "$key\\Capabilities\\FileAssociations" .pdf)" "xournal-qt.pdf"
printf '\n=== installed CLI\n'
cli_version=$(PATH="$windows/System32:$windows" "$dir/bin/xournal-qt-cli.exe" --version 2>&1 | tr -d '\r')
echo "$cli_version" | tee "$out/cli-version.log"
check "the installed CLI runs" grep -q "$version" <<< "$cli_version"

# --- 2. A .xopp opened through its file type -------------------------------------------------------------------------
printf '\n=== open a .xopp through its file type\n'
work="$out/work"
mkdir -p "$work/library"
cp "$source_dir/test/files/load/strokes.xopp" "$work/library/"
export XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME
XDG_CONFIG_HOME=$(cygpath -w "$work/config")
XDG_CACHE_HOME=$(cygpath -w "$work/cache")
XDG_DATA_HOME=$(cygpath -w "$work/data")
app_env=(QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_FORCE_STDERR_LOGGING=1)
# (start hands the file to the shell, which runs the file type's command; the app inherits this environment)
env "${app_env[@]}" XQT_SCREENSHOT_DELAY_MS=5000 "XQT_SCREENSHOT=$(cygpath -w "$out/opened-by-type.png")" \
    "$cmd" /c start "" "$(cygpath -w "$work/library/strokes.xopp")" > "$out/open-by-type.log" 2>&1
for _ in $(seq 1 120); do
    [[ -s "$out/opened-by-type.png" ]] && break
    sleep 1
done
check "the installed app opened the .xopp (opened-by-type.png)" test -s "$out/opened-by-type.png"
sleep 3  # (the app quits after its screenshot)

# --- 3. Not while the app runs ---------------------------------------------------------------------------------------
printf '\n=== the app running\n'
env "${app_env[@]}" PATH="$windows/System32:$windows" "$dir/bin/xournal-qt.exe" "$(cygpath -w "$work/library")" \
    > "$out/running-app.log" 2>&1 &
app_pid=$!
sleep 10
if kill -0 "$app_pid" 2> /dev/null; then
    echo stale > "$dir/bin/installer-test-stale.dll"
    rc=0
    run_setup while-running || rc=$?
    check "the installer gives up while the app runs (exit code 4)" same "$rc" 4
    check "and changes nothing" test -f "$dir/bin/installer-test-stale.dll"
    rm -f "$dir/bin/installer-test-stale.dll"
else
    fail "the app did not keep running (running-app.log)"
fi
kill "$app_pid" 2> /dev/null
"$windows/System32/taskkill.exe" /F /IM xournal-qt.exe > /dev/null 2>&1
wait "$app_pid" 2> /dev/null
sleep 2

# --- 4. Update -------------------------------------------------------------------------------------------------------
# As an older version: a file the new one does not have, an older version number, and .xopp given to another program
# with the choice not to open .xopp
echo stale > "$dir/bin/installer-test-stale.dll"
mkdir -p "$dir/share/xournal-qt/installer-test-old"
reg_set "$key" Version 0.0.1
reg_set "$key" OpenXopp 0 REG_DWORD
reg_set_default "$classes\\.xopp" "Other.File"
rc=0
run_setup update || rc=$?
check "the update succeeds" same "$rc" 0
check "the old version's files are gone" test ! -e "$dir/bin/installer-test-stale.dll" -a ! -e "$dir/share/xournal-qt/installer-test-old"
check "the program is there" test -f "$dir/bin/xournal-qt.exe"
check "the new version in $key" same "$(reg_value "$key" Version)" "$version"
check "the uninstall entry's new version" same "$(reg_value "$uninstall_key" DisplayVersion)" "$version"
check ".xopp stays with the other program" same "$(reg_value "$classes\\.xopp")" "Other.File"
check "the choice is kept" same "$(reg_value "$key" OpenXopp)" "0x0"
check "the settings stay" test -f "$settings/installer-test-marker"

# --- 5. No downgrade -------------------------------------------------------------------------------------------------
reg_set "$key" Version 99.0.0
rc=0
run_setup downgrade || rc=$?
check "an older version is refused (exit code 3)" same "$rc" 3
check "the installed version stays" same "$(reg_value "$key" Version)" 99.0.0
check "its files stay" test -f "$dir/bin/xournal-qt.exe"
reg_set "$key" Version "$version"

# --- 6. Uninstall ----------------------------------------------------------------------------------------------------
printf '\n=== uninstall\n'
# (the uninstaller copies itself to the temporary folder and returns at once: wait for the folder to go)
"$dir/Uninstall.exe" /S > "$out/uninstall.log" 2>&1
for _ in $(seq 1 60); do
    [[ -e "$dir" ]] || break
    sleep 1
done
check "the program folder is gone" test ! -e "$dir"
check "the Start menu entry is gone" test ! -e "$start_menu"
check "the uninstall entry is gone" eval "! reg_has '$uninstall_key'"
check "$key is gone" eval "! reg_has '$key'"
check "the file types are gone" eval "! reg_has '$classes\\xournal-qt.pdf' && ! reg_has '$classes\\xournal-qt.xopp'"
check ".pdf no longer offers Xournal Qt" eval "! reg_has '$classes\\.pdf\\OpenWithProgids' /v xournal-qt.pdf"
check "not listed in Default apps" eval "! reg_has 'HKCU\\Software\\RegisteredApplications' /v xournal-qt"
check "the other program's .xopp stays" same "$(reg_value "$classes\\.xopp")" "Other.File"
check "the settings stay" test -f "$settings/installer-test-marker"

rm -f "$settings/installer-test-marker"
"$reg" delete "$classes\\.xopp" /f > /dev/null 2>&1

printf '\n=== %s\n' "$( ((failures)) && echo "$failures check(s) failed" || echo "all checks passed")"
exit $failures
