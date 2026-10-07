# ADR-0000: Fork policy

- Status: Accepted (2026-09-19)

## Context
We want Xournal++'s tested core (model, file format, rendering, undo, tools, input logic) inside a new Qt 6 application. We also want to keep pulling upstream bug fixes.

## Decision
- A real git fork with full upstream history, branched from upstream `283c367`.
- Upstream files are edited in place, but only at small tagged seams (`xournal-qt:`, `#ifdef XOJ_NO_GTK`).
- They are never deleted, moved or reformatted.
- New code lives in `qt/`, and the build root is `qt/CMakeLists.txt`.
- The upstream root `CMakeLists.txt` stays unmodified: it had about 85 upstream commits in two years and would conflict constantly.
- Upstream is merged regularly with `qt/tools/merge-upstream.sh`; merges are gated by unit, golden and replay tests.

## Consequences
- The tree contains unused GTK sources. That is harmless: they are not compiled.
- Some seams use a small GTK compatibility shim (`qt/compat/gtkshim`) instead of edits, so the upstream files stay untouched.
- See [FORK.md](../../../FORK.md) for the day-to-day rules.
