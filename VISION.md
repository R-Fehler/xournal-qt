# Vision

> Written by the author, maintained by hand. Agents: only record here what the author said, and keep it short.
> Tasks go to [TODO.md](TODO.md); what is built and measured goes to [qt/docs/ROADMAP.md](qt/docs/ROADMAP.md).

## What xournal-qt is

A note-taking and PDF annotation app that feels great with a pen. It is Xournal++'s proven core with a modern
Qt 6 / Qt Quick frontend: tabs, libraries of documents, and touch-first controls.

## Principles

- **Pen and PDF feel come first.** This works well on Linux today. It should feel just as good on a Surface
  (Windows) and an iPad.
- **Speed through caching.** Rendering, previews and search are already fast because of caching. New features
  must not give that up.
- **Self-contained.** The app works as one bundle, with no external programs or shell tools. Libraries it needs
  are vendored (like md4c), or the feature is skipped.
- **Stay compatible with Xournal++.** `.xopp` files keep opening in upstream, and upstream merges stay cheap.
- **Keep the user's folders clean.** Caches, indexes and resources should not clutter a library or cause
  cloud-sync conflicts. It should always be possible to remove them.

## Directions

### Libraries of documents
- Any folder can be a library, not only the ones under `~/Documents/Xournal_Libraries`.
- Opening big libraries and their subfolders is instant, and moving folders stays cheap.
- Search the whole library quickly, fuzzy like fzf, with optional logical operators.
- A library holds more than notes and PDFs: Markdown and images too, and optionally every file in the folder.
  Files the app cannot handle open in the system's app.
- The cache stays small and out of the way: one hidden folder per folder, easy to remove, or kept in the app's
  cache so synced folders stay clean.

### Markdown as a first-class document
- Markdown boxes inside `.xopp` pages (exists) and plain `.md` files as documents of their own.
- A good Markdown editor: an infinite canvas, with a paginated view for printing and PDF export.
- Math (MicroTeX), images, and maybe Mermaid.
- Opens Obsidian vaults, Zettlr folders, "LLM wikis" and Markdown code docs, with Markdown links and wikilinks.
- Works on mobile.

### Cross-platform and mobile
- Android first, then iOS/iPadOS, Windows (Surface) and macOS, from one Qt Quick codebase.
- Where a platform's own ink, PDF or handwriting-recognition libraries give a better feel, use them behind common
  interfaces. Our stroke model stays our own.

### PDF as the document (far future)
- A hybrid PDF that other PDF apps show as it is (annotations as `/Ink`, inserted pages merged into the PDF). It
  also carries the full Xournal data, so it opens again with every feature, like draw.io PDFs.
- Export to plain `.xopp` for Xournal++, as a one-off or automatically.

### Further out
- Agents and CLIs that can find and read documents in large libraries: the app as a tool, pages rendered for an
  agent's context, and Markdown as a format agents can use.
- Audio recordings tied to strokes, as in upstream Xournal++.
