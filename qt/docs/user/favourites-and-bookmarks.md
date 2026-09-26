# Favourites and bookmarks

> Stars for the documents you use most, ribbons for the pages you come back to.

## Favourites (★)

Tap the star on a document's card in the library (with a mouse it appears when you point at the card), or choose
**Add to favourites** in the card's menu or in the document's **⋮** menu. **Favourites** next to the "Show" button
(in a narrower window and on a phone: **View → Only favourites**, the button with the sliders) then shows only your
starred documents, from all folders of the library; it works together with the search and with "Show".

A star is yours, not the document's: it is kept by the app, **not in the file**. The file is not changed, sync apps do
not upload it again, and others you share it with do not see your stars. Renaming or moving a document in the app keeps
its star; renaming or moving it in a file manager loses it.

## Bookmarks (ribbon)

In a document, press and hold a page in the pages sidebar (or tap its **⋮**) and tap the ribbon icon, or use
**⋮ → Bookmark this page** for the page you are on. The bookmark is named after the page's first heading, or its
chapter in the PDF's table of contents; otherwise it is called "Page 5", and that number follows the page when pages
are added, removed or moved. Tapping the ribbon icon of a bookmarked page lets you give it a name of your own or remove
it. Each change can be undone.

Bookmarked pages have a ribbon in the sidebar and in the overview of all pages, and the **Contents** sidebar lists them
at the top. The library's **Bookmarks** tab shows the bookmarked pages of all documents, grouped by document; tap one
to open the document at that page. The library search finds bookmark names too.

Bookmarks are **part of the document**:

- In a PDF with notes they are in the PDF's own outline, as the last entry "Bookmarks", so Acrobat, Okular, Preview,
  browsers and phone apps list them too. The PDF's own table of contents stays as it was.
- In a `.xopp` file they are stored with each page. Xournal++ opens such files normally, but **saving in Xournal++
  removes the bookmarks**.
- In a **Markdown file** (`.md`), and in the text of a PDF text document, a bookmark is a line in the text itself,
  right before the paragraph, heading or list it marks:

  ```markdown
  <!-- xqt:bookmark Proof of theorem 3.2 -->
  ## Proof
  ```

  It is an HTML comment, so GitHub, Obsidian, Typora, pandoc and other Markdown apps do not show it, and it moves
  with the text when you edit the file anywhere. You can type it yourself, in the app or in another editor: the
  label is everything between `xqt:bookmark` and `-->`, on one line. Leave the label out (`<!-- xqt:bookmark -->`)
  and the bookmark is named after the heading it marks, or the first heading of its page, or "Page 5". The page a
  bookmark belongs to is the page where the marked paragraph or heading starts.

  **Bookmark this page** puts such a line before the first paragraph, heading or list that starts on the page. If
  the whole page is the middle of one long block (a long list or code block), it goes before that block, so the
  bookmark is on the page where the block starts. In the app you do not see the line on the page (only the ribbon);
  the cursor skips it, and Backspace or Delete next to it removes it (Ctrl+Z brings it back). A plain text file
  (`.txt`) has no bookmarks.
