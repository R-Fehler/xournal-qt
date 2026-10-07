# Architecture

This folder will hold the architecture overview for developers (the next docs block, wave 4 of the
[2026-10 plan](../review/2026-10/README.md)): the layers and the allowed direction of dependencies between the CMake
targets, the upstream Xournal++ core in `src/` as blocks of its own and every connection between it and the Qt frontend
(direct calls, the `qt/compat` shadow headers, the seams listed in [ADR 0002](../decisions/0002-upstream-seams.md)), a
diagram linking into the source on GitHub generated from a machine-readable `architecture.yaml`, and how a tab, a pen
stroke and a page on screen travel through the modules. Until then: the module table in [AGENTS.md](../../../AGENTS.md),
the README in each `qt/src/<module>/`, and [image-caches.md](image-caches.md) (the image caches, their owners and
limits).
