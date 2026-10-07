# How work is organised

For humans and agents alike. The rules that are easy to break are in [AGENTS.md](../../../AGENTS.md); the goals are
the author's, in [VISION.md](../../../VISION.md) (nothing is added there that the author did not say).

## Branches

`master` follows upstream Xournal++; **`master-qt` is the fork's branch** ([FORK.md](../../../FORK.md) has how upstream
is merged in). Integration branches of a working session (`claude/**`) are merged into `master-qt` by the author.

## Blocks and worktrees

- Open work is in [TODO.md](../../../TODO.md), grouped into **blocks**. A block is one branch `qt/<block>` in its own
  worktree `../xournal_qt-<block>` with its own build folder
  (`git worktree add ../xournal_qt-<block> -b qt/<block> <integration branch>`); ccache shares the compiled objects
  between worktrees ([building.md](building.md)).
- Within a block, **one feature is one commit**. Before committing: the tests run for it pass, its device-only checks
  are in its feature doc's "On the device" (or the [device checklist](../testing/device-checklist.md)), and its feature doc says how it works now. Commit
  messages are plain prose: what was wrong, what changed, why.
- A **bug gets a failing test first**: show that it fails for the stated reason, then fix it.
- A **refactoring** changes structure, not behaviour: the tests that pass before pass after (except tests that only
  pin removed behaviour, which go with it), and the objectNames tests use stay.
- Blocks are usually done by agents, several in parallel, each briefed with
  [agents/block-brief.md](../agents/block-brief.md); an integrating session (or a human) plans the blocks and merges
  them. Blocks that run at the same time must touch different files.

## Merging

A block is merged into the integration branch when its full suite passed (on GitHub: [ci.md](ci.md), "Block
tests"; a failure is rerun alone before it is called a flake), and its UI tests also pass on Qt 6.8 where popups, keys
or windows changed. `qt/scripts/agents/merge-block.sh <block> "<summary>"` merges it (resolving Markdown conflicts),
and tags the merge commit `ms/<date>-<block>` with the summary (`git tag -l 'ms/*' -n1` lists them). Then the
integration branch is built and the labels both sides touched are run, the block's worktree and its remote branch are
deleted. **Nothing leaves the machine without the author's leave**: pushing other branches or tags, publishing a
release, building the `.deb` (block branches may be pushed for their tests).

## Where to record what

| What | Where |
| --- | --- |
| open work, decisions the author has to take, known flaky tests | [TODO.md](../../../TODO.md) (a done item is deleted at the merge: git keeps it) |
| how a feature works now: behaviour, settings, files, code and tests | its doc in [features/](../features/README.md) (no dates, no "the author said on …", no block history) |
| why the fork is built a certain way | an ADR in [decisions/](../decisions/README.md) |
| a change outside `qt/` (a seam in an upstream file) | [decisions/0002-upstream-seams.md](../decisions/0002-upstream-seams.md) |
| what a module is for and what it may depend on | `qt/src/<module>/README.md`, and the architecture's model `qt/docs/architecture/architecture.yaml` (then run `qt/scripts/architecture/generate.py`: [architecture/](../architecture/README.md)) |
| a user-visible change | the draft of the next release notes in [release-notes/](../release-notes/) |
| what only a real device can show | the feature doc's section "On the device"; a check of the device, the platform or the app as a whole in the [device checklist](../testing/device-checklist.md), which links to the feature docs' checks |
| a turning point of the project (rare) | [history/README.md](../history/README.md) |
| everything else (measurements, the course of a block) | the commit messages |

After moving or renaming a doc, run the link check (`python3 qt/scripts/check-doc-links.py`, [ci.md](ci.md)).

## The review of 2026-10

A critical review of the whole code base and the refactoring plan in waves:
[review/2026-10/README.md](../review/2026-10/README.md). Its open waves and later rounds are in TODO.md.
