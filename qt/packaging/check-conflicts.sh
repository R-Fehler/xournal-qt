#!/bin/sh
# Lists files of the package that another installed package already has (dpkg would refuse to install it).
# Usage: check-conflicts.sh build-deb/packages/xournal-qt_*.deb
set -e
deb="$1"
[ -f "$deb" ] || { echo "usage: $0 <package.deb>" >&2; exit 2; }
conflicts=0
for f in $(dpkg-deb -c "$deb" | awk '{print $6}' | sed 's|^\./|/|' | grep -v '/$'); do
    if owner=$(dpkg -S "$f" 2>/dev/null); then
        case "$owner" in
            xournal-qt:*) ;;  # an older version of this package
            *) echo "conflict: $owner"; conflicts=1 ;;
        esac
    fi
done
[ "$conflicts" = 0 ] && echo "no conflicts with installed packages"
exit $conflicts
