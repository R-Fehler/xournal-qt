# Handover: from the cloud sessions to a local machine (2026-10-07)

The fork was developed in Claude Code cloud sessions up to here. From now on, work continues on the author's own
server (EPYC, GPUs). This page says what state the code is in, what the first session there should do, and what
only the author can do. Delete it once those steps are done (git keeps it).

## Where things stand

- **`claude/admiring-pascal-hekja6`** is the integration branch: 89 commits ahead of `master-qt`. It holds the
  adaptive UI rework of 0.8.0, the Android Back behaviour (with a confirmation before leaving the app), the minimum
  zoom setting, and the four refactoring waves of the [2026-10 review](../review/2026-10/README.md):
  - `Main.qml`, HomeView and SettingsPage split up;
  - `AppServices` and `CanvasActions`;
  - the image caches;
  - `HybridPdf` split up;
  - atomic file I/O;
  - six bugs fixed, each with a failing test first;
  - compatibility with older xournal-qt data removed;
  - the docs restructured, with the [architecture overview](../architecture/README.md).

  Its last full local run passed (1908 tests). CI on GitHub (`xqt-build.yml`, both Qt versions) runs on every push.
- **`master-qt`** is at the 0.8.0 release. Merging the integration branch into it is the author's step. Release
  notes for the next version are drafted in [release-notes/0.9.0.md](../release-notes/0.9.0.md). `qt/CMakeLists.txt`
  still says 0.8.0; raise it when 0.9.0 is cut ([releasing.md](../development/releasing.md)).
- **Open work** is in [TODO.md](../../../TODO.md):
  - what the refactoring blocks left, checked against the code on 2026-10-07;
  - the later rounds of the review;
  - the decisions for the author;
  - the known bugs and flaky tests;
  - the open follow-ups by area.

## What did not survive the cloud container

- **The merge tags `ms/<date>-<block>`.** The cloud proxy refused to push tags, so they existed only in the container.
  The merge commits keep the same summaries: `git log --merges --oneline`.
- **The conda environments `/opt/xqt-env` (Qt 6.9) and `/opt/xqt-env68` (Qt 6.8).** These were the cloud's
  workaround for Ubuntu 24.04's Qt 6.4. A local machine uses the distribution's Qt
  ([building.md](../development/building.md)).

## First session on the local machine

1. **Get the code and the Qt you need.**
   - Clone the repository and check out the integration branch, or `master-qt` once the author has merged it.
   - Use Debian 13 (Qt 6.8) or KDE neon / Ubuntu 22.04 with `XQT_NEON=1` (Qt 6.7). `qt/scripts/linux-deps.sh`
     installs the packages.
   - On another distribution, use one of these:
     - `qt/scripts/ci-container.sh` builds and tests in the CI's own containers;
     - `qt/scripts/cloud-env.sh` sets up Qt from conda-forge.
2. **Build and run the whole suite once**, to have a baseline:

   ```sh
   cmake -S qt -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DXQT_FAST_DEV=ON
   cmake --build build-qt -j$(nproc)
   ctest --test-dir build-qt -j$(nproc)
   ```

   On the cloud's 4 cores this took about 9 minutes for the tests alone; an EPYC should take a fraction of that.
   A test that fails: compare with "Flaky tests" in TODO.md and rerun it alone before calling it a flake.
3. **Run blocks locally.** The workflow is the same as in the cloud ([workflow.md](../development/workflow.md),
   [block-brief.md](block-brief.md)):
   - Use one worktree and build folder per block. ccache shares the objects between them.
   - With many cores, run more agents at once, and run the full suite locally (`ctest -j$(nproc)`) instead of
     waiting for GitHub. `xqt-block-tests.yml` still runs when a `qt/<block>` branch is pushed.
   - `qt/scripts/build-slot.sh` is only for small machines.
   - Locally `git push origin --delete qt/<block>` and pushing tags work. Pushing still needs the author's leave
     (AGENTS.md).
4. **Fix the Android top bar.** It scrolls into a blank area at its end (TODO.md → Bugs). It needs the author's
   phone over adb ([android.md](../development/android.md)). The guess so far is that the blank area belongs to the
   emoji and "open externally" buttons, which are hidden on Android while their space is still kept.
5. **Use the GPUs.** The handwriting model's training runs are waiting for a GPU machine:
   [qt/research/hwr/train/README.md](../../research/hwr/train/README.md) (TODO.md → Handwriting).

## Only the author can do this

- **Enable GitHub Pages** once: Settings → Pages → Source: GitHub Actions. Then `xqt-pages.yml` publishes the
  interactive architecture page from `master-qt` ([ci.md](../development/ci.md)).
- **Delete the merged remote branches.** The cloud proxy refused to delete them: `qt/architecture`,
  `qt/docs-restructure`, `qt/ci-smoke`, `qt/macos-build`, `qt/windows-build`.
- **Delete the old draft releases**: the two 0.7.0 drafts, the 0.6.0 and 0.5.0 drafts and the first alpha.
- **Merge the integration branch into `master-qt`.** Cut 0.9.0 when ready ([releasing.md](../development/releasing.md)).
- **Take the decisions** listed in TODO.md → "Decisions for the author" (among them: the PDF engine).
- **Run the device checks** for what changed since 0.8.0 ([device-checklist.md](../testing/device-checklist.md)).
  Most of all: the Android Back key, including the leave dialog, and Zen mode on the phone and the Fold.
