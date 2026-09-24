#!/usr/bin/env bash
# xournal-qt: build the unsigned debug APK for Android (arm64-v8a). See qt/docs/android.md.
#
#   qt/scripts/android-build.sh          # dependencies (vcpkg), configure, build the APK
#   qt/scripts/android-build.sh deps     # only the C dependencies through vcpkg (hours the first time)
#   qt/scripts/android-build.sh apk      # only configure + build (the dependencies must be there)
#   qt/scripts/android-build.sh ksyntax  # only KSyntaxHighlighting (part of deps; Markdown code colours)
#
# Every heavy step runs with at most XQT_JOBS jobs (default 4), at low priority and, where systemd is there, in a
# user scope capped at XQT_MEM (default 6G) and XQT_JOBS cores, so that the machine stays usable.
#
# Paths (override through the environment):
#   ANDROID_SDK_ROOT  ~/Android/Sdk               ANDROID_NDK_ROOT  $ANDROID_SDK_ROOT/ndk/27.2.12479018
#   XQT_JAVA_HOME     ~/.local/jdk-17             QT_ANDROID        ~/Qt/6.11.2/android_arm64_v8a
#   QT_HOST           ~/Qt/6.11.2/gcc_64          VCPKG_ROOT        <workspace>/vcpkg (cloned when missing)
#   XQT_ANDROID_BUILD <checkout>/build-android    VCPKG_BINARY_CACHE ~/.cache/vcpkg/archives
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
qt_dir="$(dirname "$here")"
checkout="$(dirname "$qt_dir")"
workspace="$(dirname "$checkout")"

export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
export ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-$ANDROID_SDK_ROOT/ndk/27.2.12479018}"
export ANDROID_NDK_HOME="$ANDROID_NDK_ROOT"   # the name vcpkg's Android toolchain reads
# Not $JAVA_HOME: a desktop JDK (e.g. 25) breaks the Android build (jlink of the android.jar).
export JAVA_HOME="${XQT_JAVA_HOME:-$HOME/.local/jdk-17}"
export QT_ANDROID="${QT_ANDROID:-$HOME/Qt/6.11.2/android_arm64_v8a}"
export QT_HOST="${QT_HOST:-$HOME/Qt/6.11.2/gcc_64}"
export VCPKG_ROOT="${VCPKG_ROOT:-$workspace/vcpkg}"
build="${XQT_ANDROID_BUILD:-$checkout/build-android}"
jobs="${XQT_JOBS:-4}"
mem="${XQT_MEM:-6G}"
cache="${VCPKG_BINARY_CACHE:-$HOME/.cache/vcpkg/archives}"

export PATH="$JAVA_HOME/bin:$PATH"
export VCPKG_MAX_CONCURRENCY="$jobs"
export CMAKE_BUILD_PARALLEL_LEVEL="$jobs"
export VCPKG_DISABLE_METRICS=1
export VCPKG_BINARY_SOURCES="clear;files,$cache,readwrite"
# Gradle (run by androiddeployqt): the same cap on workers, no daemon left behind.
export GRADLE_OPTS="${GRADLE_OPTS:-} -Dorg.gradle.workers.max=$jobs -Dorg.gradle.daemon=false -Xmx2g"

# Heavy commands: low priority, and a memory/CPU cap when a systemd user session exists.
heavy() {
    if command -v systemd-run >/dev/null && systemctl --user show-environment >/dev/null 2>&1; then
        systemd-run --user --scope --quiet -p MemoryMax="$mem" -p CPUQuota="$((jobs * 100))%" \
            nice -n 15 "$@"
    else
        nice -n 15 "$@"
    fi
}

for d in "$ANDROID_NDK_ROOT" "$JAVA_HOME" "$QT_ANDROID" "$QT_HOST"; do
    [ -d "$d" ] || { echo "missing: $d (see qt/docs/android.md)" >&2; exit 1; }
done

# Host programs vcpkg does not download itself and that may be missing without root: bison (gettext's tools) and
# autoconf-archive (autotools ports such as gperf). Built once into $VCPKG_ROOT/host-tools.
host_tools() {
    local prefix="$VCPKG_ROOT/host-tools" gnu=https://ftp.gnu.org/gnu
    export PATH="$prefix/bin:$PATH"
    export ACLOCAL_PATH="$prefix/share/aclocal${ACLOCAL_PATH:+:$ACLOCAL_PATH}"
    export VCPKG_KEEP_ENV_VARS="ACLOCAL_PATH${VCPKG_KEEP_ENV_VARS:+;$VCPKG_KEEP_ENV_VARS}"
    mkdir -p "$prefix/src"
    if ! command -v bison >/dev/null; then
        (cd "$prefix/src" && curl -fsSLO "$gnu/bison/bison-3.8.2.tar.xz" && tar xf bison-3.8.2.tar.xz &&
            cd bison-3.8.2 && heavy sh -c "./configure --prefix='$prefix' --disable-nls -q && make -j$jobs -s && make install -s")
    fi
    if ! ls /usr/share/aclocal/ax_*.m4 "$prefix"/share/aclocal/ax_*.m4 >/dev/null 2>&1; then
        (cd "$prefix/src" && curl -fsSLO "$gnu/autoconf-archive/autoconf-archive-2024.10.16.tar.xz" &&
            tar xf autoconf-archive-2024.10.16.tar.xz && cd autoconf-archive-2024.10.16 &&
            ./configure --prefix="$prefix" -q && make -s install)
    fi
}

deps() {
    if [ ! -x "$VCPKG_ROOT/vcpkg" ]; then
        [ -d "$VCPKG_ROOT" ] || git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
        "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
    fi
    host_tools
    mkdir -p "$cache" "$build"
    local overlay_ports=()
    [ -d "$qt_dir/vcpkg/ports" ] && overlay_ports=(--overlay-ports="$qt_dir/vcpkg/ports")
    heavy "$VCPKG_ROOT/vcpkg" install \
        --x-manifest-root="$qt_dir" \
        --x-install-root="$build/vcpkg_installed" \
        --overlay-triplets="$qt_dir/vcpkg/triplets" \
        ${overlay_ports[@]+"${overlay_ports[@]}"} \
        --triplet=arm64-android --host-triplet=x64-linux-release \
        --clean-buildtrees-after-build --clean-packages-after-build
}

# KSyntaxHighlighting (KDE Frameworks; the colours of code blocks in Markdown) built against the official Qt for
# Android: vcpkg's port builds against vcpkg's own Qt and does not support Android. Needs only Qt and ECM (CMake
# files). Its indexer of the syntax definitions runs while building, so it is built for this machine too, against
# the desktop Qt. Everything lands in $build/kf6 (the library in kf6/android, static, the definitions inside).
kf_version=6.30.0
ksyntax() {
    local kf="$build/kf6"
    local src="$kf/src" prefix="$kf/android"
    if [ -f "$prefix/lib/cmake/KF6SyntaxHighlighting/KF6SyntaxHighlightingConfig.cmake" ]; then
        return
    fi
    mkdir -p "$src"
    local r
    for r in extra-cmake-modules syntax-highlighting; do
        if [ ! -d "$src/$r-$kf_version" ]; then
            (cd "$src" && curl -fsSL -o "$r-$kf_version.tar.gz" \
                "https://github.com/KDE/$r/archive/refs/tags/v$kf_version.tar.gz" && tar xf "$r-$kf_version.tar.gz")
        fi
    done
    local sh="$src/syntax-highlighting-$kf_version"
    # (KDE's clang-format hook would write into the sources while configuring)
    printf 'DisableFormat: true\nSortIncludes: false\n' > "$sh/.clang-format"
    # Two changes to the sources: the desktop rcc compresses the definitions with zstd, which the official Qt for
    # Android cannot read (--no-zstd, also for the themes through AUTORCC below); and the command line tool is not
    # needed (it would be an Android program).
    sed -i 's/COMMAND Qt6::rcc --name syntax_data/COMMAND Qt6::rcc --no-zstd --name syntax_data/' "$sh/data/CMakeLists.txt"
    sed -i 's/^    add_subdirectory(cli)$/    # add_subdirectory(cli)  # xournal-qt: not for Android/' "$sh/src/CMakeLists.txt"
    cmake -S "$src/extra-cmake-modules-$kf_version" -B "$kf/ecm-build" -DCMAKE_INSTALL_PREFIX="$kf/ecm" \
        -DBUILD_TESTING=OFF -DBUILD_DOC=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF
    cmake --install "$kf/ecm-build"
    local ecm="$kf/ecm/share/ECM/cmake"
    local common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DECM_DIR="$ecm" -DBUILD_TESTING=OFF
        -DKF_SKIP_PO_PROCESSING=ON -DCMAKE_DISABLE_FIND_PACKAGE_Python=ON -DCMAKE_DISABLE_FIND_PACKAGE_XercesC=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Quick=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Widgets=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Qt6PrintSupport=ON)
    heavy cmake -S "$sh" -B "$kf/host-build" "${common[@]}" -DCMAKE_PREFIX_PATH="$QT_HOST" \
        -DKSYNTAXHIGHLIGHTING_USE_GUI=OFF
    heavy cmake --build "$kf/host-build" --target katehighlightingindexer -j "$jobs"
    heavy cmake -S "$sh" -B "$kf/android-build" "${common[@]}" -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_TOOLCHAIN_FILE="$QT_ANDROID/lib/cmake/Qt6/qt.toolchain.cmake" -DQT_HOST_PATH="$QT_HOST" \
        -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
        -DBUILD_SHARED_LIBS=OFF -DKATEHIGHLIGHTINGINDEXER_EXECUTABLE="$kf/host-build/bin/katehighlightingindexer" \
        -DCMAKE_AUTORCC_OPTIONS=--no-zstd
    heavy cmake --build "$kf/android-build" -j "$jobs"
    cmake --install "$kf/android-build"
}

apk() {
    # The preset (qt/CMakePresets.json) with this script's paths, which may come from the environment.
    heavy cmake --preset android-arm64-debug -S "$qt_dir" -B "$build" \
        -DCMAKE_TOOLCHAIN_FILE="$QT_ANDROID/lib/cmake/Qt6/qt.toolchain.cmake" \
        -DQT_HOST_PATH="$QT_HOST" \
        -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
        -DQT_CHAINLOAD_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
        -DVCPKG_INSTALLED_DIR="$build/vcpkg_installed" \
        -DKF6SyntaxHighlighting_DIR="$build/kf6/android/lib/cmake/KF6SyntaxHighlighting"
    heavy cmake --build "$build" --target apk -j "$jobs"
    local out="$build/android-build/build/outputs/apk/debug/android-build-debug.apk"
    [ -f "$out" ] || out="$(find "$build" -name '*.apk' -newer "$build/CMakeCache.txt" | head -1)"
    echo "APK: $out ($(du -h "$out" | cut -f1))"
    echo "Install: adb install -r '$out'"
}

case "${1:-all}" in
    deps) deps; ksyntax ;;
    ksyntax) ksyntax ;;
    apk) apk ;;
    all) deps; ksyntax; apk ;;
    *) echo "usage: $0 [deps|ksyntax|apk|all]" >&2; exit 2 ;;
esac
