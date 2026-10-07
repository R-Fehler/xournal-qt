# Brief for block agents

How work is split: an **integrator** (one agent session, or a human) plans blocks, gives each block to an agent in its
own git worktree, merges the finished branches into the integration branch and releases. A **block agent** builds ONE
block. This page is what the integrator hands every block agent (point it here in the prompt, plus the block's spec).

## Your setup
- Worktree `../xournal_qt-<block>` (next to the main checkout) on branch `qt/<block>`, created by the integrator.
  Work only there.
- Read first: `AGENTS.md` and `VISION.md` (binding), the spec your prompt names (TODO.md or a plan under
  `qt/docs/review/`), and the docs of your area.

## Rules that matter most
- Upstream files (everything outside `qt/`) stay untouched. An unavoidable seam: tiny, marked `xournal-qt:`, listed
  in `qt/docs/decisions/0002-upstream-seams.md`.
- One feature, one commit. A bug gets a failing test first (show it fails for the stated reason, then fix it).
- Before each commit: the tests you ran for it pass; `qt/docs/testing/device-checklist.md` gets the manual checks a
  device needs. Commit messages are plain prose (what was wrong / what changed / why), with the attribution lines the
  session asks for.
- Never touch the author's data (`~/.config/xournalpp`, `~/.config/xournal-qt`, their documents); never write into
  `test/files`. Tests run off-screen with temporary config and cache folders.
- QML items a test needs carry an `objectName`.
- Every new cache has an owner and a limit; background work at idle priority; never hold the document lock while
  drawing a PDF.
- Use no Qt API newer than **6.7** (the oldest Qt a release is built with: KDE neon) and no QML property names that are
  JS globals (`console`, …). E.g. `AbstractButton.click()` is 6.8: tests emit `clicked`/`triggered` instead.
- A refactoring changes structure, not behaviour: the tests that pass before pass after (except tests that only pin
  removed behaviour, which go with them). Keep the objectNames tests use.
- Don't add to VISION.md. In your last commit: TODO.md (your items `[x]` with a one-line note of what is left), the
  feature doc of what changed, a user-visible change in the next draft under `qt/docs/release-notes/`.
- Decide obvious things yourself. If something is genuinely the author's decision, choose the conservative option,
  write it under "Decisions for the author" in your report, and go on.

## Building and testing
- Configure once in your worktree: `cmake -S qt -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  -DXQT_FAST_DEV=ON` (plus your environment's arguments; in a cloud session `source /opt/xqt-env/cloud-env.env` and
  use `$XQT_CMAKE_ARGS -DXQT_FAST_DEV=ON`). The source dir is `qt/`. FAST_DEV: QML not compiled ahead of time, `-g1`,
  lld — a QML edit rebuilds in seconds.
- Build only the targets you need. Run the tests of your area while you work: ctest labels `unit session canvas quick
  shell markdown hwr audio ui` (`-L '^ui$'` for the UI tests only), or `-R` filters. Don't rebuild or retest after
  edits to docs alone.
- **The full suite runs on GitHub, not on your machine**: when the block is done, push your branch
  (`git push -u origin qt/<block>`); `.github/workflows/xqt-block-tests.yml` builds it and runs the whole suite in four
  shards. Report the run's result (the GitHub tools: list the runs of `xqt-block-tests.yml` for your branch; read the
  failed jobs' logs). A failure: reproduce it locally with `-R`, fix, push again. Push only your own `qt/<block>`
  branch; never other branches, tags or releases.
- Flaky tests are listed in TODO.md → "Flaky tests": rerun a failure alone before calling it a flake.
- When you touch QML popups, keys or window states, also run your UI tests against Qt 6.8 if the environment has it
  (the cloud sessions had `/opt/xqt-env68`): Qt 6.7/6.8 differ (e.g. a closing popup still takes keys).

## Final report (your answer)
Short and plain: per commit, what it does and the tests run (pass/fail); the GitHub run of the full suite; what you
could not verify (device-only things go to the device checklist); decisions for the author (numbered, each with the
choice you made); known gaps; where you found the spec or a review wrong.

## For the integrator
- Merge with `qt/scripts/agents/merge-block.sh <block> "<summary>"` (resolves Markdown conflicts, lists the rest).
  Build the merged integration branch, run what the merge could have broken (the labels of both sides), push; the
  integration branch's CI (`xqt-build.yml`) runs both Linux Qt versions with QML compiled ahead of time. Delete the
  block's worktree and its remote branch afterwards (`git push origin --delete qt/<block>`).
- Run two or three agents at once on a 4-core machine; more on a bigger one. Each full local build is ~1.2 GB.
