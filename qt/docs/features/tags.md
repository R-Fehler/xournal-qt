# Tags (`qt/tags`)

Chosen by the author (2026-10-04, idea A13 of [the ideas of 2026-10](../history/README.md), as in Apple Notes, Obsidian and
Zotero): `#tag` in typed text and Markdown, keywords of PDFs; indexed per folder; a Tags tab in the library like
Favourites; `tag:` in the search; tags on the cards.

## What a tag is

`#name`: letters (any script), digits, `-`, `_`, and `/` for a tag inside another (`#course/math` is inside
`#course`), with at least one letter (`#3`, `#1984`, `#2026-10` are none). The `#` starts the text or follows a space
or an opening mark (`( [ { " ' “ ‘ « , ; :`), so a URL's fragment (`page#section`), `C#` and a Markdown heading
(`# Title`, a space after `#`) are none; `#a#b` is none. Tags are compared without case (`#Exam` is `#exam`); the
spelling shown is the one found first. At most 100 characters. (`qt/src/session/Tags.*`)

Where they are read:

| Where | How |
| --- | --- |
| Typed text elements of notes (`.xopp`, PDFs with notes) | the plain text |
| Markdown boxes, sticky notes' texts, pages of PDF text documents | through the parser: not in inline code, code blocks, formulas or HTML; a heading's text may have some (`## Lecture #exam`) |
| `.md` files | as above, plus an Obsidian front matter's `tags:` (or `tag:`): a list (`- a`), `[a, b]` or words; its other values are not looked at |
| PDFs (plain, with notes, text documents, archive PDFs), and the PDF a `.xopp` uses | the document information's `/Keywords` and the XMP metadata's `dc:subject`: split at commas or semicolons (without any, at spaces); a keyword becomes a tag with other characters made `-` ("Machine learning" → `Machine-learning`) |

Handwriting is not read for tags (the recogniser's words are unsure; write `#tag` in a text box). Plain text files
(`.txt`) have none.

## The index

Each document's tags go into its folder's small "notes" pack (`LibraryIndex`, as bookmarks and to-dos): `tags` (those
of its text) and `pdfTags` (its PDF's keywords, kept with the PDF's stamp: a `.xopp` saved again does not read its PDF
again; a PDF with notes, whose stamp changes with every save, has its keywords read again with qpdf, only the trailer
and the document information). `LibraryIndex::tagged()`, `tagsOf()`, `textTagsOf()`, `hasTag()` and `tagChanges()`
are what the views and the search use.

## The library

- **Tags tab** (the switch: Library, Recent, Favourites, **Tags**, Bookmarks, To-dos; `TagsView.qml`,
  `LibraryTagsModel`): every tag with how many documents have it; nested tags folded under their parent (an arrow;
  "Unfold all"); a parent counts each document that has it or a tag inside it once. A text filters the tags (a nested
  one shows with its parents). It follows the library's "Show" filter and the Favourites star; "Only in <folder>"
  counts only the library's current folder and its subfolders. On a phone the switch has no room for another tab: Tags
  is in the library's ▾ menu there.
- **A tap on a tag** sets the library's tag filter (`LibraryModel::tagFilter`) and shows the library: the documents
  with the tag or one inside it, of the current folder and its subfolders (from the Tags tab without "Only in": the
  library's top), without folders; combined with Show, the star and the search. A chip `#tag ✕` beside the breadcrumbs
  says so; a tap on it takes the filter away. Going into a folder narrows the list.
- **Cards** show their tags on the preview, above "last read": one on a narrow card, up to three on a wide one, then
  `+N`; all of them in a tip.

## Search

- Fuzzy syntax ([library.md](library.md), "Fuzzy search"): `tag:name` holds for a document with the tag or one inside
  it (`tag:course` finds `#course` and `#course/math`), `tag:course/` only for the tags inside it; never for a name;
  `!tag:draft` leaves documents out; with `|`, parentheses and other terms as any term. On the pages of a document it
  marks `#name` where it is written (`#course` and `#course/math`, not `#coursework`).
- Plain search: `tag:name` words filter, the rest is searched (`tag:exam` alone lists every document with the tag);
  also when only names are searched.

## Tags in files: "Tags…"

"Tags…" in a card's menu (notes, PDFs, Markdown) and in the document's ⋮ → Document (`TagsDialog.qml`):

- **A PDF** (plain, with notes, a text document, an archive PDF) gets tags without typing into it: they are written
  as its **keywords** (`PdfKeywords::write`), as Zotero and Acrobat read them: an incremental update
  (`IncrementalPdf`) with the document information's `/Keywords` and, where the file has XMP metadata with keywords,
  its `dc:subject` and `pdf:Keywords`. An archive PDF's XMP is written anew from the document information
  (`ArchivePdf::update`), so it stays PDF/A. Keywords that are still wanted keep their spelling ("Machine learning"
  stays as written). Nothing else of the file changes; its earlier bytes stay as they were. An encrypted PDF is not
  changed. The app's own saves of a PDF with notes keep the keywords: an incremental save leaves them, a save in full
  takes them over from the file it writes over. One writer of a file at a time (`fileio::FileWriteLock`): a save of
  the same PDF that starts while its tags are written waits for them, and the reverse. On a PDF with notes that keeps
  its versions, the tags' update belongs to the current version (`HybridPdf::keepHistoryIn`): it is not listed as
  another app's change, the day's version is still replaced by later saves that day, and versions still take a
  message.
  - Open in a tab without unsaved changes: written, and the tab reads the file again in its place. With unsaved
    changes: refused with "save it first" (the tab's next save would otherwise write the file without them).
  - The dialog lists the file's tags (✕ removes one), a field to add one (Enter), the library's tags as suggestions
    (the most used first, filtered by what is typed), and the tags typed in the document (changed where they are
    written).
- **A Xournal++ file** (`.xopp`): only the `#tags` typed in it. The dialog lists them and says how to add one.
  Decided as the least invasive way that keeps tags in the file: an attribute of our own on the root element would
  need an upstream seam and Xournal++ drops it when it saves the file; a tag in the document's title is not kept by
  Xournal++ either; writing the keywords into the `.xopp`'s PDF would change the user's original PDF, which the
  Xournal++ mode never does. A typed `#tag` is ordinary text that every version of Xournal++ keeps.
- **A Markdown file**: its `#tags` and its front matter's `tags:`; the dialog says so (not edited from the dialog).

## Files

| What | Where |
| --- | --- |
| What a tag is, Markdown and front matter, queries | `qt/src/session/Tags.*` |
| A PDF's keywords: read and write | `qt/src/session/PdfKeywords.*` |
| The index | `qt/src/shell/Library.*` (`Tagged`, `tagged()`, `tagsOf()`, `hasTag()`, `tagChanges()`) |
| `tag:` in the fuzzy syntax | `qt/src/session/FuzzyQuery.*`, the help `FuzzyHelp.qml` |
| The tag filter, the cards' tags | `qt/src/shell/LibraryModel.*` (`tagFilter`, `TagsRole`) |
| The Tags tab | `qt/src/shell/LibraryTags.*`, `qt/src/app/qml/TagsView.qml` (in `HomeView.qml`) |
| "Tags…" | `qt/src/app/AppTags.cpp`, `qt/src/app/qml/TagsDialog.qml` |
| Tests | `qt/tests/shell/TagsTest.cpp`, `qt/tests/ui/TagsTest.cpp` |

## Open

- Renaming a tag across the library (its typed `#tags` in every document) is not offered.
- Tags of handwriting (a handwritten `#exam` read by the recogniser) are not read.
- A `.md` file's front matter is not edited from "Tags…".
- Untested on a device: Zotero and Acrobat showing the keywords written; the Fold 7's ▾ menu entry.
