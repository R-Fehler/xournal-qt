/*
 * xournal-qt: tags (qt/docs/tags.md): `#tag` in typed text, Markdown boxes, sticky notes and Markdown files (with an
 * Obsidian front matter), keywords of PDFs (document information and XMP), read into the library index's "notes"
 * pack.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <fstream>
#include <memory>

#include <QCborMap>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/PdfKeywords.h"
#include "session/StickyNote.h"
#include "session/Tags.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryCache.h"

using namespace xqt;

namespace {
void writeFile(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << bytes;
}

std::unique_ptr<Text> textAt(const std::string& text, bool markdown, double x = 40, double y = 60) {
    auto t = std::make_unique<Text>();
    t->setText(text);
    t->setFont(XojFont("Sans", 11));
    if (markdown) {
        t->setWrap(300);
    }
    t->move(x - t->getOrigin().x, y - t->getOrigin().y);
    return t;
}

QStringList list(std::initializer_list<const char*> items) {
    QStringList out;
    for (const char* i: items) {
        out << QString::fromUtf8(i);
    }
    return out;
}
}  // namespace

/// A PDF of `pages` pages with these keywords in its document information ("": none) and, if given, an XMP packet
/// with this dc:subject
void makeTaggedPdf(const fs::path& p, const char* keywords, const std::vector<std::string>& subject = {},
                   int pages = 1) {
    fs::create_directories(p.parent_path());
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    if (*keywords) {
        cairo_pdf_surface_set_metadata(s, CAIRO_PDF_METADATA_KEYWORDS, keywords);
    }
    cairo_t* cr = cairo_create(s);
    for (int i = 0; i < pages; ++i) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, ("page " + std::to_string(i + 1)).c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    if (subject.empty()) {
        return;
    }
    std::string xmp = "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
                      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
                      "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
                      "<dc:subject><rdf:Bag>";
    for (const auto& k: subject) {
        xmp += "<rdf:li>" + k + "</rdf:li>";
    }
    xmp += "</rdf:Bag></dc:subject>\n</rdf:Description></rdf:RDF></x:xmpmeta>\n<?xpacket end=\"w\"?>";
    const fs::path tmp = p.string() + ".tmp";
    {
        QPDF q;
        q.processFile(p.string().c_str());
        QPDFObjectHandle meta = q.newStream(xmp);
        meta.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/Metadata"));
        meta.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));
        q.getRoot().replaceKey("/Metadata", meta);
        QPDFWriter w(q, tmp.string().c_str());
        w.write();
    }
    fs::rename(tmp, p);
}

// `#tag` in a plain text: letters, digits, - _ /, at least one letter; not in a URL, not C#, not a heading
TEST(Tags, inPlainText) {
    EXPECT_EQ(tags::inText(u"Lecture #exam and #course/math, (#Physik) \"#todo\""),
              list({"exam", "course/math", "Physik", "todo"}));
    EXPECT_EQ(tags::inText(u"#a-b_c #a-b_c #A-B_C"), list({"a-b_c"})) << "once, case ignored";
    EXPECT_EQ(tags::inText(u"https://example.org/page#section C# a#b #3 #1984 #2026-10 # Title ## Sub"), QStringList());
    EXPECT_EQ(tags::inText(u"#course/ #/x #a//b"), list({"course", "x", "a/b"}));
    EXPECT_EQ(tags::inText(u"#bücher #日本"), list({"bücher", "日本"})) << "any letters";
    EXPECT_EQ(tags::inText(u"#a#b"), QStringList());
}

// Markdown: the text without code, formulas and HTML; headings are no tags, their text may have some; front matter
TEST(Tags, inMarkdown) {
    EXPECT_EQ(tags::inMarkdown("# Title\n\nText #one, **#two** and [#three](https://x.org/#frag)\n\n"
                               "`#code` and $#math$\n\n```\n#block\n```\n\n    #indented\n\n<div>#html</div>\n\n"
                               "## Lecture #four\n\n- [ ] todo: #five\n"),
              list({"one", "two", "three", "four", "five"}));
    // Obsidian's front matter: a list, flow style, words; its other values are not looked at
    EXPECT_EQ(tags::inMarkdown("---\ntitle: \"#notatag\"\ntags:\n  - course/math\n  - \"exam\"\n"
                               "color: '#ff0000'\n---\nText #body\n"),
              list({"course/math", "exam", "body"}));
    EXPECT_EQ(tags::inMarkdown("---\ntags: [physics, \"#optics\"]\n---\n"), list({"physics", "optics"}));
    EXPECT_EQ(tags::inMarkdown("\xEF\xBB\xBF---\r\nTags: one two\r\n...\r\n"), list({"one", "two"}));
    EXPECT_EQ(tags::inMarkdown("---\ntags: not closed\n"), QStringList());
    EXPECT_EQ(tags::inMarkdown("<!-- xqt:plain -->\nA plain text #plain\n"), list({"plain"}));
}

// Keywords as tags; tag queries; the tag: terms of a plain search
TEST(Tags, keywordsAndQueries) {
    EXPECT_EQ(tags::fromKeywords(u"Machine learning, #optics; C++ ;; 2026"), list({"Machine-learning", "optics", "C"}));
    EXPECT_EQ(tags::fromKeywords(u"physics optics physics"), list({"physics", "optics"}));
    EXPECT_EQ(tags::fromKeyword(u" Kurs / Mathe "), "Kurs/Mathe");
    EXPECT_TRUE(tags::matches(u"course", u"course"));
    EXPECT_TRUE(tags::matches(u"Course/Math", u"#course"));
    EXPECT_FALSE(tags::matches(u"coursework", u"course"));
    EXPECT_FALSE(tags::matches(u"course", u"course/"));
    EXPECT_TRUE(tags::matches(u"course/math", u"course/"));
    EXPECT_FALSE(tags::matches(u"course/math", u"math"));
    const auto q = tags::splitQuery("kalman tag:exam  TAG:course/ filter");
    EXPECT_EQ(q.tags, list({"exam", "course/"}));
    EXPECT_EQ(q.rest, "kalman filter");
}

// A PDF's keywords: its document information's and its XMP's dc:subject
TEST(Tags, keywordsOfAPdf) {
    QTemporaryDir tmp;
    const fs::path dir(tmp.path().toStdString());
    makeTaggedPdf(dir / "a.pdf", "physics, optics", {"exam", "Optics", "Tom &amp; Jerry"});
    const auto k = pdfkeywords::read(dir / "a.pdf");
    EXPECT_TRUE(k.read);
    EXPECT_EQ(k.info, "physics, optics");
    EXPECT_EQ(k.subject, list({"exam", "Optics", "Tom & Jerry"}));
    EXPECT_EQ(k.tags(), list({"physics", "optics", "exam", "Tom-Jerry"}));
    makeTaggedPdf(dir / "b.pdf", "");
    EXPECT_EQ(pdfkeywords::tagsOf(dir / "b.pdf"), QStringList());
    writeFile(dir / "c.pdf", "not a PDF");
    EXPECT_FALSE(pdfkeywords::read(dir / "c.pdf").read);
}

// The index reads the tags of typed text, Markdown boxes, sticky notes, Markdown files and PDF keywords into "notes":
// another index has them without reading a document
TEST(Tags, theIndexReadsTheTags) {
    QTemporaryDir tmp;
    const fs::path root = fs::path(tmp.path().toStdString()) / "Library";
    sticky::installDrawer();
    {
        Document doc(nullptr);
        auto page = std::make_shared<XojPage>(595.0, 842.0);
        auto* md = new Layer();
        md->setName(std::string(xoj::markdown::LAYER_NAME));
        page->getLayers().push_back(md);
        md->addElement(textAt("Notes #course/math\n\n`#notatag`", true));
        auto* plain = new Layer();
        page->getLayers().push_back(plain);
        plain->addElement(textAt("typed #exam", false, 40, 300));
        sticky::Look look;
        look.rect = {100, 400, 200, 140};
        Layer* note = sticky::makeNote(look);
        note->addElement(textAt("#sticky", true, sticky::textOrigin(look).x, sticky::textOrigin(look).y));
        page->getLayers().push_back(note);
        doc.addPage(page);
        fs::create_directories(root);
        ASSERT_TRUE(DocumentSession::writeDocument(doc, root / "lecture.xopp").ok);
    }
    writeFile(root / "Sub" / "plan.md", "---\ntags: [project]\n---\n# Plan #week\n");
    makeTaggedPdf(root / "Sub" / "paper.pdf", "physics; Optics");
    writeFile(root / "Sub" / "none.md", "Nothing.\n");
    {
        LibraryIndex index(root);
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
        EXPECT_EQ(index.tagsOf(root / "lecture.xopp"), list({"course/math", "exam", "sticky"}));
        EXPECT_EQ(index.tagsOf(root / "Sub" / "plan.md"), list({"project", "week"}));
        EXPECT_EQ(index.tagsOf(root / "Sub" / "paper.pdf"), list({"physics", "Optics"}));
        EXPECT_TRUE(index.hasTag(root / "lecture.xopp", u"course"));
        EXPECT_FALSE(index.hasTag(root / "lecture.xopp", u"math"));
        EXPECT_EQ(index.tagged().size(), 3u) << "documents without tags are left out";
        index.flush();
    }
    LibraryIndex again(root);
    const quint64 changes = again.tagChanges();
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 0) << "from the packs";
    EXPECT_EQ(again.tagsOf(root / "Sub" / "paper.pdf"), list({"physics", "Optics"}));
    EXPECT_EQ(again.tagsOf(root / "lecture.xopp"), list({"course/math", "exam", "sticky"}));
    EXPECT_NE(again.tagChanges(), changes);
    // The PDF changes: its keywords are read again
    makeTaggedPdf(root / "Sub" / "paper.pdf", "chemistry");
    fs::last_write_time(root / "Sub" / "paper.pdf",
                        fs::last_write_time(root / "Sub" / "paper.pdf") + std::chrono::seconds(5));
    const quint64 before = again.tagChanges();
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.tagsOf(root / "Sub" / "paper.pdf"), list({"chemistry"}));
    EXPECT_NE(again.tagChanges(), before);
}

// Entries indexed before tags were have none stored: read once more; a plain PDF only for its keywords
TEST(Tags, entriesFromBeforeAreReadAgainOnce) {
    QTemporaryDir tmp;
    const fs::path root = fs::path(tmp.path().toStdString()) / "Library";
    writeFile(root / "plan.md", "#one\n");
    makeTaggedPdf(root / "paper.pdf", "two");
    {
        LibraryIndex index(root);
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
        index.flush();
    }
    // The pack as an older build wrote it: no "tags"
    {
        CacheLocation where(root);
        auto notes = Packs::read(where.dirOf(root), LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT);
        ASSERT_TRUE(notes);
        for (const char* name: {"plan.md", "paper.pdf"}) {
            QCborMap entry = notes->value(QString::fromUtf8(name)).toMap();
            ASSERT_TRUE(entry.contains(QStringLiteral("tags"))) << name;
            entry.remove(QStringLiteral("tags"));
            entry.remove(QStringLiteral("pdfTags"));
            notes->insert(QString::fromUtf8(name), entry);
        }
        ASSERT_TRUE(Packs::write(where.dirOf(root), LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT, *notes, true));
    }
    LibraryIndex again(root);
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 1) << "the Markdown file";
    EXPECT_EQ(again.keywordsRead(), 1) << "the plain PDF: only its keywords";
    EXPECT_EQ(again.tagsOf(root / "plan.md"), list({"one"}));
    EXPECT_EQ(again.tagsOf(root / "paper.pdf"), list({"two"}));
    again.flush();
    LibraryIndex third(root);
    third.update(DocumentFiles::scanRecursive(root));
    third.waitForDone();
    EXPECT_EQ(third.documentsRead(), 0) << "once";
    EXPECT_EQ(third.keywordsRead(), 0);
}
