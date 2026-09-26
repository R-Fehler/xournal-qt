# Getting the Markdown out of a PDF text document

> For PDF text documents written in xournal-qt (the "PDF files" way of keeping documents, or Settings → Documents →
> "New text documents: PDF document"; built in `qt/md-pdf`). The text is there as `name.md` now; images
> (`name.assets/…`) come with image support in Markdown (`qt/md-images`).

A text document saved as a PDF looks like any PDF in every viewer. Inside, it also carries its text as plain
Markdown, so it can go back to Markdown editors (Obsidian, Typora, VS Code, GitHub, …) without xournal-qt. Copying
the text out of the pages would not give you that: it loses headings, lists, links and formulas. The Markdown is in
the PDF's **attachments**:

| Attachment | What it is |
|---|---|
| `name.md` | the text as Markdown, updated on every save (`name` is the PDF's name without `.pdf`) |
| `name.assets/…` | its images (once images in Markdown are supported), e.g. `name.assets/figure-1.png` |
| `document.xopp` | xournal-qt's own data (text, handwriting, pages); you do not need it for Markdown |

The links in `name.md` point to `name.assets/…`. So the images must end up in a folder called `name.assets` next
to `name.md`, and then every Markdown editor shows them.

## The easy way: xournal-qt
Open the PDF in xournal-qt and choose **⋮ → Export as Markdown**. It writes `name.md` (and, once images are
supported, the `name.assets` folder) where you choose; when you keep your documents as Xournal++ files it writes
`name.md` next to the document instead, and asks before it replaces a file of that name. This works on Linux,
Windows and Android.

## With a PDF viewer
Open the viewer's attachments list (usually a paperclip icon in the side bar), save `name.md`, then create a folder
`name.assets` next to it and save the images into it. Some viewers keep the folder part of an image's name and some
only save the file name; either way, the images belong in `name.assets`.

| Viewer | Attachments | Where |
|---|---|---|
| Adobe Acrobat Reader (Windows, macOS) | yes | side bar → paperclip → right-click a file → Save Attachment |
| Firefox (all systems) | yes | side bar → "Show Attachments" (paperclip) → click a file to save it |
| Okular (KDE) | yes | side bar → Embedded Files → right-click → Save As |
| GNOME Document Viewer / Papers (Evince) | yes | side bar menu → Attachments → right-click → Save As |
| Foxit PDF Reader | yes | side bar → Attachments → Save |
| Chrome, Edge (Windows' default viewer) | to check | recent versions may list attachments; if not, use another way here |
| macOS Preview, Safari | no | Preview does not show PDF attachments: use Acrobat Reader, Firefox or the command line |
| iPhone and iPad (Files, Books) | no | use the Acrobat Reader app or xournal-qt |
| Android (Google's PDF viewer, Drive, Files) | no | use the Acrobat Reader app (to check) or xournal-qt |

## On the command line
With qpdf (Linux: the `qpdf` package; macOS: `brew install qpdf`; Windows: from qpdf's release page):

```sh
qpdf --list-attachments name.pdf                        # what is inside
qpdf --show-attachment=name.md name.pdf > name.md       # the Markdown
mkdir -p name.assets
for a in $(qpdf --list-attachments name.pdf | sed -n 's|^\(name\.assets/[^ ]*\) .*|\1|p'); do
    qpdf --show-attachment="$a" name.pdf > "$a"         # each image, into name.assets/
done
```

With poppler (`pdfdetach`, in `poppler-utils`): `pdfdetach -list name.pdf`, then `pdfdetach -save N name.pdf` for
each file, and move the images into `name.assets`.

## Going the other way
Open a `.md` in xournal-qt and choose **⋮ → Open as PDF document**: a new `name.pdf` is made next to it (`name
(2).pdf` when that name is taken) and opened; the `.md` stays as it is. The PDF carries `name.md` again (and, once
images are supported, its images as `name.assets/…`).

Handwriting on a text page stays where you drew it when the text above it gets longer or shorter: move it yourself
if it should go along.
