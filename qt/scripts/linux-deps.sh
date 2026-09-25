#!/usr/bin/env bash
# xournal-qt: install what the build needs on Debian and Ubuntu (also used by the CI workflows, so the package names
# live in one place).
#
#   qt/scripts/linux-deps.sh                     # this machine
#   XQT_NEON=1 qt/scripts/linux-deps.sh          # add KDE neon's repository first: Qt 6.7 and KF6 on Ubuntu 22.04
#   XQT_SYSTEM_QPDF=1 qt/scripts/linux-deps.sh   # also the system's qpdf, for -DXQT_SYSTEM_QPDF=ON (12 or newer)
#
# Qt 6.5 or newer is needed. Ubuntu 24.04 has 6.4, so use Debian 13 (Qt 6.8), Ubuntu 25.04, or KDE neon (XQT_NEON=1).
# qpdf is built with the app (qt/cmake/XqtQpdf.cmake: its source is downloaded when the build is configured); it
# needs zlib and libjpeg.
set -euo pipefail

SUDO=""
[ "$(id -u)" = 0 ] || SUDO=sudo
export DEBIAN_FRONTEND=noninteractive

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends ca-certificates curl gnupg lsb-release

if [ "${XQT_NEON:-0}" = 1 ]; then
    # KDE neon's packages (Qt 6.7, KF6) for the Ubuntu release of this machine
    codename="$(. /etc/os-release && echo "${UBUNTU_CODENAME:-jammy}")"
    curl -fsSL https://archive.neon.kde.org/public.key | $SUDO gpg --dearmor -o /etc/apt/trusted.gpg.d/neon.gpg
    echo "deb http://archive.neon.kde.org/user ${codename} main" | $SUDO tee /etc/apt/sources.list.d/neon.list
    $SUDO apt-get update
fi

# Without these there is no build at all
$SUDO apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git file \
    qt6-base-dev qt6-base-private-dev qt6-declarative-dev libxkbcommon-dev \
    libglib2.0-dev libcairo2-dev libpango1.0-dev libpoppler-glib-dev libgdk-pixbuf-2.0-dev \
    libxml2-dev libzip-dev zlib1g-dev libjpeg-dev
if [ "${XQT_SYSTEM_QPDF:-0}" = 1 ]; then
    $SUDO apt-get install -y --no-install-recommends libqpdf-dev
fi

# Wanted, but the build and the tests also run without them (their names differ between the distributions: KDE neon
# has one qt6-declarative for all QML modules, Debian and Ubuntu split them up).
optional=(
    qt6-declarative-private-dev
    libqt6svg6 libqt6svg6-dev qt6-svg-plugins
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-dialogs
    qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtqml-workerscript
    libkf6syntaxhighlighting-dev
    fonts-dejavu-core fonts-noto-core
)
for package in "${optional[@]}"; do
    $SUDO apt-get install -y --no-install-recommends "$package" || echo "xournal-qt: without ${package}"
done

echo "xournal-qt: $(cmake --version | head -1), Qt $(dpkg-query -W -f='${Version}' qt6-base-dev 2>/dev/null || echo '?')"
