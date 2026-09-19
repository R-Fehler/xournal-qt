#!/usr/bin/env bash
# Merge upstream Xournal++ into the current branch, rebuild the Qt frontend and run the test suites.
# Usage: qt/tools/merge-upstream.sh [upstream-ref]   (default: upstream/master)
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
REF="${1:-upstream/master}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-qt}"
cd "$ROOT"

if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "error: working tree has uncommitted changes" >&2
    exit 1
fi

git fetch upstream
BASE_BEFORE="$(git merge-base HEAD "$REF")"
echo "Merging $REF (previous merge base: ${BASE_BEFORE:0:10})"
if ! git merge --no-ff --no-edit "$REF"; then
    cat >&2 <<'MSG'
Merge has conflicts. Resolution rules (see FORK.md):
  - never delete upstream files (keep "theirs" for files the Qt build does not compile)
  - for seams tagged "xournal-qt:", re-apply the seam on top of the upstream change
  - update qt/docs/adr/0002-upstream-seams.md if a seam changed
Then: git commit && re-run this script with the tests only (SKIP_MERGE=1 is not implemented; just build + ctest).
MSG
    exit 2
fi

cmake -S qt -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$BUILD_DIR"
if [[ -f "$BUILD_DIR/CTestTestfile.cmake" ]]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
echo "Upstream merge done. New merge base: $(git merge-base HEAD "$REF" | cut -c1-10)"
