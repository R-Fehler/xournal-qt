#!/usr/bin/env bash
# Runs "$@" once one of two build slots is free (AGENTS.md: at most two worktree builds at a time), in a user scope
# with a hard memory cap (XQT_SLOT_MEM, default 4G) and 3 CPUs, at low priority: a build that needs more memory is
# stopped instead of taking the machine down (2026-09-26: five agents ran it out of 16 GB RAM + 15 GB swap).
# Usage: qt/scripts/build-slot.sh cmake --build build-qt -j3 --target xqt-ui-tests
d="${XQT_SLOT_DIR:-${TMPDIR:-/tmp}/xqt-build-slots-$(id -u)}"
mem="${XQT_SLOT_MEM:-4G}"
mkdir -p "$d"
run() {
    if command -v systemd-run >/dev/null && systemctl --user show-environment >/dev/null 2>&1; then
        systemd-run --user --scope --quiet -p MemoryMax="$mem" -p MemorySwapMax=512M -p CPUQuota=300% \
            nice -n 15 "$@"
    else
        nice -n 15 "$@"
    fi
}
while :; do
    for i in 1 2; do
        exec {fd}>"$d/slot$i"
        if flock -n "$fd"; then
            run "$@"
            rc=$?
            flock -u "$fd"
            exec {fd}>&-
            exit $rc
        fi
        exec {fd}>&-
    done
    sleep 5
done
