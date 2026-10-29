#!/usr/bin/env bash
# xournal-qt: Qt from qt.io (the official binaries, through aqtinstall) for a Linux system whose own Qt is too old -
# Ubuntu 22.04 has 6.2 - and KSyntaxHighlighting (the colours of code blocks in Markdown) built against it. The CI's
# Ubuntu 22.04 job and the AppImage build with it.
#
#   XQT_QT_DOWNLOAD=1 qt/scripts/linux-deps.sh   # first: the system's libraries, without its Qt
#   qt/scripts/linux-qt.sh /opt/qt               # Qt in /opt/qt/<version>/gcc_64 (XQT_QT_VERSION, default below)
#   cmake -S qt -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/qt/6.11.2/gcc_64 ...
#
# Nothing is downloaded or built again when the folder has it already (the CI caches it). KSyntaxHighlighting is
# installed into Qt's own prefix, so CMake and linuxdeploy find it with Qt.
set -euo pipefail

version="${XQT_QT_VERSION:-6.11.2}"
root="${1:-/opt/qt}"
kf_version=6.30.0  # (as qt/scripts/android-build.sh)
mkdir -p "$root"
root="$(cd "$root" && pwd)"
qt="$root/$version/gcc_64"

if [ ! -x "$qt/bin/qmake" ]; then
    python3 -m venv "$root/aqt-venv"
    "$root/aqt-venv/bin/pip" install -q aqtinstall
    # Qt Multimedia for the recordings (qt/docs/features/audio.md), Qt Shader Tools for the dark pages
    # (qt/docs/features/dark-pages.md); Qt Base has Qt Declarative, Qt SVG and Qt Wayland already
    "$root/aqt-venv/bin/aqt" install-qt linux desktop "$version" linux_gcc_64 -m qtmultimedia qtshadertools -O "$root"
fi

if [ ! -f "$qt/lib/cmake/KF6SyntaxHighlighting/KF6SyntaxHighlightingConfig.cmake" ]; then
    work="$root/kf6-build"
    rm -rf "$work"
    mkdir -p "$work"
    for r in extra-cmake-modules syntax-highlighting; do
        curl -fsSL -o "$work/$r.tar.gz" "https://github.com/KDE/$r/archive/refs/tags/v$kf_version.tar.gz"
        tar -C "$work" -xf "$work/$r.tar.gz"
    done
    sh="$work/syntax-highlighting-$kf_version"
    # (KDE's clang-format hook would write into the sources while configuring)
    printf 'DisableFormat: true\nSortIncludes: false\n' > "$sh/.clang-format"
    cmake -S "$work/extra-cmake-modules-$kf_version" -B "$work/ecm-build" -DCMAKE_INSTALL_PREFIX="$work/ecm" \
        -DBUILD_TESTING=OFF -DBUILD_DOC=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF
    cmake --install "$work/ecm-build"
    cmake -S "$sh" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$qt" \
        -DCMAKE_PREFIX_PATH="$qt" -DECM_DIR="$work/ecm/share/ECM/cmake" -DBUILD_TESTING=OFF \
        -DKF_SKIP_PO_PROCESSING=ON -DCMAKE_DISABLE_FIND_PACKAGE_Python=ON -DCMAKE_DISABLE_FIND_PACKAGE_XercesC=ON \
        -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Quick=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt6PrintSupport=ON
    cmake --build "$work/build" -j "$(nproc)"
    cmake --install "$work/build"
    rm -rf "$work"
fi

echo "xournal-qt: Qt ${version} from qt.io in ${qt}, KSyntaxHighlighting ${kf_version}"
