#!/usr/bin/env bash
# xournal-qt: makes the README's pictures again (README.md here): the demo library, the shots of the UI tests on it
# (off-screen, no display needed), and the pictures composed from them into this folder.
#
#   qt/docs/screenshots/make-shots.sh <build folder>
#
# Needs: xqt-ui-tests in the build folder (any build type); node with Playwright and python3 with Hershey-Fonts and
# Pillow (demo/make-demo.sh, compose.py); for the picture of the handwriting search ONNX Runtime: the build's own, or
# XQT_ONNXRUNTIME=<path of libonnxruntime.so.1> (qt/scripts/linux-onnxruntime.sh downloads it).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="$(cd "$1" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
bash "$here/demo/make-demo.sh" "$work/library"
QT_QPA_PLATFORM=offscreen XQT_SHOTS="$work/shots" XQT_SHOTS_DEMO="$work/library" "$build/xqt-ui-tests" \
    --gtest_filter='ReadmeShots.*:MainWindowTest.shotOfMarkdown:PluginsUiTest.readmeShotOfAPlot'
python3 "$here/compose.py" "$work/shots" "$here"
