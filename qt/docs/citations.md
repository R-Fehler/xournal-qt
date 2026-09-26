# Citations and arXiv

Status: **design note of `qt/citations`** (2026-09-26); built as described below ("What is built" at the end lists
where it differs). The author (2026-09-25/26): "following citations is too hard". Reading a paper, the reader meets
"[12]" and wants the paper behind it: is it in the library already? If not, where is it? This block makes that a few
taps: from the selected text to the paper beside the notes.

Not now: `.bib` files (the user flow is still being thought out), Zotero.

## 1. Actions on selected text

Selected text is either **PDF text** (the PDF text tool, or a long press on a word) or **our own text** (a Markdown
box, a text element, a `.md` being written: the editor's selection). Both offer the same actions:

| Action | What it does |
| --- | --- |
| **Find this paper** | the library, by title (section 2); no hit: Google Scholar and arXiv |
| **Search in Google Scholar** | the browser, `https://scholar.google.com/scholar?q=<text>` |
| **Translate** | the browser, with the translator of Settings (below) |
| **arXiv 1706.03762** | only when the text has an arXiv ID: its page, or its PDF downloaded into the library (section 3) |

- **Where:** the pill of selected PDF text gets a **Look up** button (a magnifier on a book) that opens a menu with
  these actions. The context pill (right click, long press) has **Look up…** while text is selected there, PDF text
  or text being written. The reference's pills have the same.
- **The URL is always shown before anything is opened** (the author's rule). Each menu entry that leads to the web
  shows its address as a second, smaller line (elided in the middle). Choosing it opens a small confirmation with the
  whole address (selectable, wrapped), **Open**, **Copy address** and **Cancel**, and "Don't ask again". Without the
  confirmation (after "Don't ask again"), the address is still on the menu entry; Settings turns the confirmation
  back on. On a phone the confirmation is the same, narrower.
- The browser: `QDesktopServices::openUrl`, through `SystemApps` so tests never start one. Only `http`/`https`.
- The query is the selected text, whitespace collapsed, hyphenation at line ends joined ("hyphen- ation" →
  "hyphenation"), cut to 500 characters for Scholar and 1,500 for translators (URL lengths).

**Translate.** Settings → Documents → "Web and citations":
- *Translator*: Google Translate (default), DeepL, Bing Translator, or "Custom…" with an address in which `{text}`
  and `{lang}` are replaced (`https://translate.google.com/?sl=auto&tl={lang}&text={text}&op=translate`). A custom
  address must be `http(s)` and have `{text}`.
- *Translate into*: the system's language (default, from `QLocale::system()`: "de" for de_DE) or a chosen one.

## 2. The paper of a reference, in the library

**Selecting a bibliography entry** and choosing "Find this paper" opens the **Find paper** sheet:

1. **The title of the entry** is guessed (`cite::guessTitle`, `qt/src/session/Citation.*`), shown in an editable
   field (typing searches again), with the raw text below it as the fallback:
   - the entry is cleaned first: line-end hyphens joined, whitespace collapsed, a leading label dropped (`[12]`,
     `12.`, `(12)`, `[Vas+17]`);
   - **quoted** (IEEE, ACM's older style, many German styles): the text in “…”, "…", „…“, ‚…‘, «…», '…' if it has at
     least two words;
   - **APA**: after `(2017).` / `(2017a).` / `(n.d.).` up to the end of that sentence;
   - **ACM, Chicago author-date**: after `. 2017. ` up to the end of the sentence;
   - **LNCS / Springer** (`Vaswani, A., Shazeer, N.: Attention is all you need. In: …`): after the colon that ends the
     author list;
   - **otherwise** (IEEE without quotes, arXiv references, Harvard): the entry is split into sentences at `. `, `? `,
     `! ` - not after initials (`A.`), `et al.`, `u. a.`, or abbreviations (`vol.`, `pp.`, `Proc.`, `S.`, `Hrsg.`,
     …) -, the author sentences at the start are skipped (names, initials, commas, `and` / `und` / `&`, `et al.`),
     and the first sentence that is not a venue (`In`, `In:`, `Proc…`, `arXiv`, `Journal`, `IEEE Trans…`,
     `vol.`/`pp.`, only a year) is the title.
   - The guess never is empty: without a better one it is the cleaned entry.
2. **The library is searched by title, not by file name** (arXiv files are just numbers). Each indexed document
   has, in its `notes.pack` entry:
   - `title`: the PDF's `/Title` from its document information, when it looks like one (not empty, not a file name
     like `paper.dvi` / `Microsoft Word - x.docx`, not `untitled`, not only digits);
   - `heading`: the largest-font text of the PDF's first page (poppler's text attributes: the runs with the largest
     font size, in reading order, at most 300 characters). The arXiv stamp in the margin (`arXiv:1706.03762v7
     [cs.CL] …`, often the largest text of the page) is skipped;
   - and, already there, the first page's text.

   Both are read where the index reads the PDF's text (`LibraryIndex::read`, the worker thread), from a poppler
   instance of its own (`qt/src/session/PdfTitle.*`), and tied to the PDF's stamp like its text. **No format bump**:
   an entry written before has no `title` key, and only its title is read, once (the PDF opened, its first page's
   text attributes; not the document, not its text), so an existing library is not read anew. A Markdown file's
   title is its first heading (from the index, nothing stored).
3. **Matching** (`LibraryIndex::findTitle`, on a worker thread): the words of the guessed title (case folded as the
   search folds them, without stop words of English and German) are compared with each candidate - the `/Title`,
   the heading, the file name (without extension), and the first 400 characters of the first page's text (where a
   title is; a reference list further down would make every citing paper a hit):
   - a word counts 1 when it is the candidate's word, or one starts with the other with at most 3 letters more
     ("network"/"networks"); 0.7 when it is the word with a typo (WordMatch's Damerau-Levenshtein distance with the
     typo tolerance of Settings → Search);
   - a title-like candidate (`/Title`, heading, name) scores 0.75 × the share of the query's words found + 0.25 ×
     the share of its own words that were found (so a long heading that happens to contain the words ranks lower);
     the first page scores 0.9 × the share found (its length says nothing);
   - a title-like candidate of 3 words or more also scores 0.95 × the share of its words found in the whole entry:
     a document whose title is in the entry is found even when the guess went wrong (IEEE without quotes). Once the
     title in the sheet is changed by hand, only the changed title is matched;
   - the document's score is its best candidate's; hits from 0.5 up, best first, at most 20. The documents shown (the
     tab's and the reference's) are left out: the reference is in one of them, which has its title in its text.
4. **The hits**: each row has the document's title (the heading or `/Title`, else the name), its folder and file
   name, the score as a percentage, and three actions: **Reference** (beside the notes, `openAsReference`), **Tab**
   (a new tab), **Copy link** (the app's document link, `copyDocumentLink`: pasted onto the page it makes a link
   marker, into Markdown `[title](path)`, as `qt/docs/links.md` says).
5. **No hit** (and under the hits, smaller): **Search in Google Scholar** and **Search arXiv** for the title, each
   with its address shown.

## 3. arXiv

- **IDs are recognised** (`cite::arxivIds`) in the selected text: `arXiv:2401.12345`, `arXiv:2401.12345v2`,
  `2401.12345` (new style, 4+4 or 4+5 digits, month 01-12), `hep-th/9901001` and `math.AG/0601001` (old style, with
  the archive), and `arxiv.org/abs/…`, `arxiv.org/pdf/…(.pdf)` URLs. A bare new-style number counts only with
  "arXiv" in the text or a URL (a page number like `1234.5678` in a reference is not an ID); the rest always.
- **Search by title**: the export API, `https://export.arxiv.org/api/query?search_query=ti:w1+AND+ti:w2…&max_results=10`
  (the title's words without stop words, at most 8), an Atom feed. The results list shows title, authors, year and
  ID; each has **Download into the library** (its PDF URL shown) and **Open on arxiv.org** (browser, URL shown).
- **An ID alone** asks the API for its title first (`id_list=<id>`), to name the file.
- **Download into the library**: `https://arxiv.org/pdf/<id>` (with its version if the text had one) is fetched into
  the current library folder, or another one chosen in the sheet (the library's folder chooser), named by the
  paper's title: `Attention Is All You Need (1706.03762).pdf`. The name: the title with `/ \ : * ? " < > |` and
  control characters removed, whitespace collapsed, at most 120 characters (cut at a word), trailing dots and
  spaces dropped; the ID without its version, `/` of an old-style ID as `_` (`hep-th_9901001`). A file of that name
  that is there already is **not downloaded again** (the sheet offers to open it); another file of that name gets
  " (2)". The body must start with `%PDF-`, else it is an error (arXiv sends HTML pages for withdrawn papers). It is
  written through `QSaveFile` on a worker thread. Then the library picks it up (its watcher; the sheet asks it to
  look now), it is indexed, and the sheet offers **Open as reference** and **Open in a tab**.
- **Networking is opt-in.** Setting `networkAccess` (Settings → Documents → "Web and citations"): *Ask* (the
  default), *On*, *Off*. The first network use while *Ask* explains in one dialog what goes where: "Searching arXiv
  sends the words of the title to export.arxiv.org; downloading fetches the PDF from arxiv.org. Nothing else is
  sent, no account is used." **Allow** sets *On*; **Not now** does nothing. *Off*: the arXiv actions say it is
  turned off in Settings. The browser actions (Scholar, Translate, "Open on arxiv.org") are not networking of the
  app: they follow the confirmation of section 1.
- **The URL is always shown before anything is fetched**: the search's address in the sheet next to its button, a
  download's PDF address on its row.
- **The network layer**: `QNetworkAccessManager` (asynchronous: nothing waits on the UI thread), behind an interface
  (`NetFetch`) that tests replace with a fake (no test touches the network). Every request has a timeout (20 s for
  the API, 120 s for a PDF) and a readable error ("arXiv did not answer within 20 s", HTTP status, "not a PDF").
- **arXiv's API rules**: a user agent that names the app and where to find it (`xournal-qt/<version>
  (+https://github.com/R-Fehler/xournal-qt)`), and **at most one request every 3 s** (a queue: a second request
  waits its turn; the sheet says "Waiting for arXiv…").

## Settings (the `xournalQt` part of the settings file)

| Key | Values | Default |
| --- | --- | --- |
| `webConfirm` | ask before opening a web address | on |
| `translateService` | `google`, `deepl`, `bing`, or a custom address with `{text}` / `{lang}` | `google` |
| `translateLanguage` | a language code, `""` = the system's | `""` |
| `networkAccess` | `ask`, `on`, `off` | `ask` |

## Code

- `qt/src/session/Citation.*`: title guess, arXiv IDs, web addresses, the Atom parser, the download name (pure
  functions; `CitationTest` in `-L session`).
- `qt/src/session/PdfTitle.*`: `/Title` and the largest-font text of the first page (poppler-glib).
- `qt/src/shell/Library.*`: the `title` / `heading` of an entry, `LibraryIndex::findTitle`.
- `qt/src/shell/NetFetch.*`: the network interface, the real one (`QNetworkAccessManager`, timeouts, user agent) and
  the 3 s queue for arXiv.
- `qt/src/shell/Citations.*`: `app.citations`, the QML side: look-up addresses, the confirmation, the title search,
  arXiv search and download, the opt-in.
- QML: `LookUpMenu.qml` (the menu of the pills), `WebConfirm.qml`, `FindPaperSheet.qml`.
