#!/usr/bin/env bash
# xournal-qt: install what the build needs on Debian and Ubuntu (also used by the CI workflows, so the package names
# live in one place).
#
#   qt/scripts/linux-deps.sh                        # this machine, with the system's Qt
#   XQT_QT_DOWNLOAD=1 qt/scripts/linux-deps.sh      # without the system's Qt: Qt comes from qt.io (linux-qt.sh)
#   XQT_SYSTEM_QPDF=1 qt/scripts/linux-deps.sh      # also the system's qpdf, for -DXQT_SYSTEM_QPDF=ON (12 or newer)
#
# Qt 6.5 or newer is needed. Ubuntu 24.04 has 6.4 and Ubuntu 22.04 has 6.2: there use Qt from qt.io
# (XQT_QT_DOWNLOAD=1 here, then qt/scripts/linux-qt.sh), or Debian 13 (Qt 6.8), Ubuntu 25.04 or newer (26.04: Qt 6.10).
# qpdf is built with the app (qt/cmake/XqtQpdf.cmake: its source is downloaded when the build is configured); it
# needs zlib and libjpeg.
set -euo pipefail

SUDO=""
[ "$(id -u)" = 0 ] || SUDO=sudo
export DEBIAN_FRONTEND=noninteractive
download_qt="${XQT_QT_DOWNLOAD:-0}"

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends ca-certificates curl gnupg lsb-release

# Without these there is no build at all
$SUDO apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git file \
    libxkbcommon-dev \
    libglib2.0-dev libcairo2-dev libpango1.0-dev libpoppler-glib-dev libgdk-pixbuf-2.0-dev \
    libxml2-dev libzip-dev zlib1g-dev libjpeg-dev
if [ "$download_qt" = 1 ]; then
    # Qt from qt.io brings its own libraries, but not what they link against on the system: OpenGL and EGL (also
    # their headers: Qt's CMake files look for them), fontconfig, D-Bus, CUPS (the print plugin), PulseAudio and
    # XRandR (Qt Multimedia and its FFmpeg plugin), the X11/XCB libraries of the xcb platform, and Python for
    # aqtinstall (qt/scripts/linux-qt.sh). The AppImage takes them from here.
    $SUDO apt-get install -y --no-install-recommends \
        python3 python3-venv xz-utils \
        libgl-dev libegl-dev libopengl-dev libglx-dev libgl1 libegl1 \
        libfontconfig1 libfreetype6 libdbus-1-3 libcups2 libpulse0 libxrandr2 \
        libx11-xcb1 libxcb1 libxcb-cursor0 libxcb-glx0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
        libxcb-render-util0 libxcb-shape0 libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxcb-xkb1 libxkbcommon-x11-0
else
    $SUDO apt-get install -y --no-install-recommends qt6-base-dev qt6-base-private-dev qt6-declarative-dev
fi
if [ "${XQT_SYSTEM_QPDF:-0}" = 1 ]; then
    $SUDO apt-get install -y --no-install-recommends libqpdf-dev
fi

# Wanted, but the build and the tests also run without them (their names differ between the distributions)
optional=(
    fonts-dejavu-core fonts-noto-core
)
if [ "$download_qt" != 1 ]; then
    # (Qt from qt.io has all of these; KSyntaxHighlighting is then built by linux-qt.sh)
    optional+=(
        qt6-declarative-private-dev
        libqt6svg6 libqt6svg6-dev qt6-svg-plugins
        qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-dialogs
        qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtqml-workerscript
        libkf6syntaxhighlighting-dev
        qt6-multimedia-dev  # (audio recordings: the microphone and the speaker; without it recording is not offered)
        qt6-shadertools-dev qt6-shader-baker  # (dark pages drawn by a shader; without them on the CPU: qt/docs/features/dark-pages.md)
    )
fi
for package in "${optional[@]}"; do
    $SUDO apt-get install -y --no-install-recommends "$package" || echo "xournal-qt: without ${package}"
done

if [ "$download_qt" = 1 ]; then
    echo "xournal-qt: $(cmake --version | head -1), Qt from qt.io (qt/scripts/linux-qt.sh)"
else
    echo "xournal-qt: $(cmake --version | head -1), Qt $(dpkg-query -W -f='${Version}' qt6-base-dev 2>/dev/null || echo '?')"
fi
