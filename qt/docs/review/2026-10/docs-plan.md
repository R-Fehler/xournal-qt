# Plan: the docs structure (for wave 4)

Infra review §9.3 and block B14 proposed a structure for `qt/docs/`. Wave 1 (`qt/docs-structure`) did only what helps
the refactoring now: the old history deleted (one page, [history/README.md](../../history/README.md)), TODO.md down
to open work, AGENTS.md current, the device checklist cut to the device-only checks, a testing guide
([testing/README.md](../../testing/README.md)). The moves below wait for wave 4, because the refactoring changes the
structure the docs describe, and because moving a doc means editing the code comments that cite its path (about 460
files cite `qt/docs/<name>.md`), which would collide with the refactoring blocks. Do them together with the
architecture overview, once the code has settled.

## Target layout

```
qt/docs/
  README.md            the entry page: what xournal-qt is (2 paragraphs), the repo layout (upstream src/, the fork in
                       qt/, the seams in qt/compat and ADR 0002), where to start reading, links to everything below
  architecture/        the overview generated from architecture.yaml (wave 4), the glossary, data on disk
  features/            one doc per user-facing feature: how it works now (behaviour, settings, files, code, tests)
  decisions/           the ADRs (today adr/), plus new ones distilled from the feature docs: the hybrid PDF format,
                       the library index, memory owners, background save, canvas tiles, the text mode removed
  development/         building.md (deps, FAST_DEV, ccache, worktrees, build-slot, the cloud and Qt 6.8 envs),
                       testing.md (today testing/README.md), workflow.md (blocks, worktrees, merging, where to record
                       what: today in AGENTS.md), releasing.md, android.md, windows.md, macos.md, the two platform
                       roadmaps, performance-logging.md
  testing/             device-checklist.md
  user/, release-notes/, review/, screenshots/, history/   as they are
qt/src/<module>/README.md   ≤40 lines each: what it is for, main classes with one line each, what it may depend on,
                            threads, its tests (label) and docs
```

## Moves

| Today | Then |
| --- | --- |
| `adaptive-layout`, `adopt-annotations`, `annotations-md`, `audio`, `bookmarks`, `canvas-rotation`, `citations`, `color-palettes`, `curtain`, `dark-pages`, `groups`, `handwriting-search`, `hidpi`, `hover-cursors`, `hybrid-pdf`, `library`, `links`, `markdown-boxes`, `md-editor`, `md-images`, `md-pdf`, `note-space`, `onboarding`, `page-files`, `page-rotation`, `pen-gestures`, `presenter-view`, `quick-note`, `reference-view`, `snip`, `stickers`, `sticky-notes`, `tags`, `templates`, `timeline`, `todos`, `toolbox`, `zen` (`.md`) | `features/` |
| `adr/*` | `decisions/` |
| `releasing`, `android`, `android-roadmap`, `windows`, `windows-roadmap`, `macos` (`.md`), `testing/performance-logging.md`, `testing/README.md` (as `testing.md`) | `development/` |
| `research/handwriting-recognition.md` | `qt/research/hwr/research.md` (next to the trials it describes) |

## How (tried in wave 1, then rolled back)

1. One commit of `git mv` only, so history follows.
2. One commit that rewrites every reference with a script: relative Markdown links (also inside the moved files,
   resolved from their old folder), and the paths in comments of C++, QML, CMake, scripts, workflows and packaging
   files (`qt/docs/X.md`, `docs/X.md`, `../docs/X.md`). In wave 1 this touched 461 files, comment lines only; about
   460 comment lines then pass 120 columns (rewrap or accept).
3. A link check over every Markdown file of the fork (every relative link resolves), in CI from then on.
4. Then the new pages: the entry page, the module READMEs, building and workflow guides (AGENTS.md shrinks to rules
   and links), and the feature docs trimmed to current behaviour: no "the author (date) said", block names,
   "What is built / where it differs" or measurement logs unless they explain a decision; a decision worth keeping
   becomes an ADR.
5. Root README.md: a short pointer to `qt/docs/README.md` at the top, and the file listed as a seam in ADR 0002 (its
   top half is the fork's, the rest upstream's).
