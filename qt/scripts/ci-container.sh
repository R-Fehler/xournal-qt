#!/usr/bin/env bash
# xournal-qt: build and test in the containers of the Linux CI (.github/workflows/xqt-build.yml, and the "packages"
# jobs of xqt-release.yml), to see a failure of GitHub's runners on this machine.
#
#   qt/scripts/ci-container.sh debian build [targets…]   # Debian 13 (Qt 6.8); no target: everything, as the CI does
#   qt/scripts/ci-container.sh jammy test -R AdaptiveLayoutTest.menusAreSheetsOnPhones  # Ubuntu 22.04, Qt 6.11 from qt.io
#   qt/scripts/ci-container.sh debian run build/xqt-ui-tests --gtest_filter='AdaptiveLayoutTest.*'
#   qt/scripts/ci-container.sh debian shell | clean     # a shell in the container; delete its build folder
#
# As on GitHub: the image and the package list (qt/scripts/linux-deps.sh, built into a local image xqt-ci:<name>),
# root in the container, no locale set (C), the checkout at /__w/xournal-qt/xournal-qt (mounted read-only here), the
# build type of the CI (XQT_CI_BUILD_TYPE, RelWithDebInfo; the release builds Release). The build folder and a ccache
# live in ${XQT_CI_DIR:-~/.cache/xqt-ci}/<name>, outside the checkout. Docker when it runs, else rootless podman.
# The container gets XQT_CI_JOBS CPUs (default 3) and 4 GB; on a machine shared with other builds run this through
# qt/scripts/build-slot.sh as well. Upstream's ColorPalette.testDefaultWrite writes into test/files, so it fails on the
# read-only checkout; XQT_CI_WRITABLE=1 mounts it writable (as on GitHub).
set -euo pipefail

usage() { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ $# -ge 2 ] || usage
name="$1"
what="$2"
shift 2
case "$name" in
    debian) base=docker.io/library/debian:trixie download=0 ;;
    jammy) base=docker.io/library/ubuntu:22.04 download=1 ;;
    *) usage ;;
esac

here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$here/../.." && pwd)"
work="${XQT_CI_DIR:-$HOME/.cache/xqt-ci}/$name"
jobs="${XQT_CI_JOBS:-3}"
image="xqt-ci:$name"
checkout=/__w/xournal-qt/xournal-qt
if [ -n "${XQT_CI_ENGINE:-}" ]; then
    engine="$XQT_CI_ENGINE"
elif command -v docker >/dev/null && docker info >/dev/null 2>&1; then
    engine=docker
else
    engine=podman
fi
mkdir -p "$work"

# The image: the base of the CI with what linux-deps.sh installs (rebuilt when the script changes), plus ccache, and
# for jammy Qt from qt.io (linux-qt.sh, in /opt/qt as on GitHub)
ctx="$(mktemp -d)"
trap 'rm -rf "$ctx"' EXIT
cp "$here/linux-deps.sh" "$here/linux-qt.sh" "$ctx/"
qt_version="$(sed -n 's/^version="\${XQT_QT_VERSION:-\([0-9.]*\)}"$/\1/p' "$here/linux-qt.sh")"
qt_step="true"
prefix_env=()
if [ "$download" = 1 ]; then
    qt_step="bash /linux-qt.sh /opt/qt"
    # (and the CMake of linux-qt.sh: Ubuntu 22.04's is too old for KDE's CMake files)
    prefix_env=(-e "CMAKE_PREFIX_PATH=/opt/qt/$qt_version/gcc_64"
        -e "PATH=/opt/qt/tools/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
fi
cat >"$ctx/Dockerfile" <<EOF
FROM $base
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends git ca-certificates ccache
COPY linux-deps.sh linux-qt.sh /
RUN XQT_QT_DOWNLOAD=$download bash /linux-deps.sh && $qt_step && apt-get clean
EOF
# (the host's network: a bridge has no name server on some machines)
"$engine" build -q --network=host -t "$image" "$ctx" >/dev/null

limits=(--memory=4g)
# (rootless podman on cgroup v2 usually has no cpu controller delegated: there the -j of the build and ctest limit it)
[ "$engine" = docker ] && limits+=(--cpus="$jobs" --init)
mode=ro
[ "${XQT_CI_WRITABLE:-0}" = 1 ] && mode=rw
tty=()
[ -t 0 ] && [ -t 1 ] && tty=(-it)
in_container() {
    "$engine" run --rm "${tty[@]}" "${limits[@]}" --network=host \
        -v "$src:$checkout:$mode" -v "$work:/work" -w "$checkout" \
        -e CI=true -e HOME=/github/home -e CCACHE_DIR=/work/ccache ${prefix_env[@]+"${prefix_env[@]}"} \
        "$image" bash -c "mkdir -p \$HOME && $1" bash "${@:2}"
}

configure="[ -f /work/build/build.ninja ] || cmake -S qt -B /work/build -G Ninja \
    -DCMAKE_BUILD_TYPE=${XQT_CI_BUILD_TYPE:-RelWithDebInfo} -DXQT_BUILD_SPIKES=OFF"
case "$what" in
    build)
        targets=()
        for t in "$@"; do targets+=(--target "$t"); done
        in_container "$configure && cmake --build /work/build -j $jobs \"\$@\"" "${targets[@]}"
        ;;
    test)
        in_container "cd /work && ctest --test-dir /work/build -j $jobs --output-on-failure --timeout 300 \"\$@\"" "$@"
        ;;
    run)
        in_container "cd /work && \"\$@\"" "$@"
        ;;
    shell)
        in_container "cd /work && exec bash"
        ;;
    clean)
        in_container "rm -rf /work/build"
        ;;
    *) usage ;;
esac
