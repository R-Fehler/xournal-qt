# Password-protected PDFs

> Open PDFs that need a password, and protect your own with one.

## Opening

When a PDF needs a password, the app asks for it. A wrong password is said so, and you can try again; **Cancel** leaves
the PDF closed. The password is kept only while the document is open, in memory: it is not saved in the settings, the
recent files or anywhere else. Open the PDF again after closing it and the app asks again.

Some PDFs open without a password but carry **restrictions** set by their author (no printing, no copying of the
text). The app respects them: printing and copying text say that the author does not allow it. You can still write
on such a PDF; saving keeps its restrictions.

Notes kept as a `.xopp` next to a protected PDF ask for the PDF's password when they are opened.

## Protecting a document

**⋮ → Document → Protect with a password…** protects the document's PDF (a PDF, or a PDF with notes). Type the password
twice. **Restrict what others can do with it** adds restrictions (printing, copying text, changes) and needs a second
password, the one that lifts them; PDF apps that respect restrictions (Acrobat, Preview) apply them, others may not.

From then on every PDF app (Acrobat, Preview, a browser) asks for the password, and so does this app. The PDF is
encrypted with AES-256. **If the password is forgotten, nobody can open the document again**, not even this app.

- Unsaved changes are saved into the PDF first. The PDF is written anew, so the earlier versions it kept (version
  history) are removed; the versions you save from then on are kept, encrypted.
- **⋮ → Document → Change or remove the password…** changes it, or removes it (the PDF opens without one again).
- **Share** has **Protect with a password**: the PDF with notes is sent as a copy that needs the password you type
  there; your document stays as it is. A protected document is always shared with its password.
- **Export as plain PDF** of a protected document is protected with the same password. An archive PDF (PDF/A) cannot
  have a password, so the archive export of a protected document has none (its dialog says so). Xournal++ cannot open
  protected PDFs: "For Xournal++" asks you to remove the password first, and a protected document is never saved as a
  `.xopp`.
- **Extract to a new document** and **Split** of a protected document give PDFs with notes protected with the same
  password (never a `.xopp`). **Export pages as pictures** is not offered for it: pictures cannot have a password.
  **Copy page as image** works as the snip does (the clipboard is not a file).
- **Insert pages from a file** asks for the password of a protected PDF. The pages inserted are then part of your
  document, protected only if your document is.

## What stays private

While a protected document is open, nothing of it is written unencrypted to the app's cache: its automatic saves are
encrypted with the same password, its page pictures stay in memory, the library does not read it (its card shows a
lock and its text is not found by the library's search), and handwriting recognised in it is not kept. If the app
crashes, the last automatic save is offered again and asks for the password.

Printing sends an unencrypted copy to the printer, as every app does; it is removed as soon as the printing system has
taken it. "Without annotations" prints the PDF's own pages only.
