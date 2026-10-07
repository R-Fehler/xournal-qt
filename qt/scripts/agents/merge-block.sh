#!/usr/bin/env bash
# Merge a block branch into the current (integration) branch: qt/scripts/agents/merge-block.sh <block> "<summary>"
# Conflicts in Markdown docs are resolved by keeping both sides (TODO.md: the block's own item from the branch, the
# rest from HEAD; if that does not apply, it is left: keeping both sides of TODO.md brings back items HEAD removed).
# Every other conflict is left for a human: the script lists it.
# A clean merge is committed and tagged ms/<date>-<block>. See qt/docs/agents/block-brief.md.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
cd "$(git rev-parse --show-toplevel)"
b=$1; msg=$2
git merge --no-ff -q "qt/$b" -m "Merge qt/$b: $msg" >/dev/null 2>&1
for f in $(git diff --name-only --diff-filter=U); do
  case $f in
    TODO.md) python3 "$here/todo-merge.py" "$b" >/dev/null 2>&1 && git add "$f" ;;
    *.md) python3 "$here/keep-both.py" "$f" >/dev/null && git add "$f" ;;
  esac
done
left=$(git diff --name-only --diff-filter=U)
if [ -z "$left" ]; then
  git commit -q --no-edit 2>/dev/null
  git tag -a "ms/$(date +%F)-$b" -m "$msg" 2>/dev/null
  echo "MERGED $b"
else
  echo "LEFT $b (resolve, git add, git commit --no-edit): $left"
fi
