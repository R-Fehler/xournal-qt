/*
 * xournal-qt: tags (qt/docs/tags.md): `#tag` in typed text, Markdown boxes, sticky notes and Markdown files (with an
 * Obsidian front matter), keywords of PDFs (document information and XMP), read into the library index's "notes"
 * pack.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
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
#include "session/FuzzyQuery.h"
#include "session/HybridPdf.h"
#include "session/PdfKeywords.h"
#include "session/StickyNote.h"
#include "session/Tags.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryIndex.h"
#include "shell/LibraryCache.h"
#include "shell/LibraryModel.h"
#include "support/TestSupport.h"

using xqt::test::writeFile;

using namespace xqt;

namespace {

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

// `tag:` in the fuzzy syntax: a term of its own (never in names), negated with !, marked as `#tag` in text
TEST(Tags, theFuzzySyntaxHasTagTerms) {
    const FuzzyQuery q("kalman tag:#Course/ !tag:draft");
    ASSERT_TRUE(q.isValid());
    ASSERT_EQ(q.terms().size(), 3u);
    EXPECT_TRUE(q.terms()[1].isTag());
    EXPECT_EQ(q.terms()[1].text, "course/");
    EXPECT_TRUE(q.terms()[2].isTag());
    EXPECT_TRUE(q.terms()[2].negated);
    EXPECT_FALSE(q.positive(2));
    EXPECT_FALSE(q.matchName(u"course draft").found[1]) << "tags are not in names";
    const textmatch::Term marked = FuzzyQuery("tag:exam").terms()[0].textTerm();
    EXPECT_EQ(textmatch::count(u"#exam and #examples, #exam/oral", marked.text, marked.bounds), 2);
    EXPECT_FALSE(FuzzyQuery("tag:").isValid()) << "an empty tag is left out";
}

// The library's search: `tag:name` in the fuzzy and in the plain search, also when only names are searched
TEST(Tags, theLibrarySearchFindsTags) {
    QTemporaryDir tmp;
    const fs::path root = fs::path(tmp.path().toStdString()) / "Library";
    writeFile(root / "kalman.md", "# Kalman filter #course/math\n");
    writeFile(root / "Sub" / "draft.md", "Kalman, a draft #course #draft\n");
    writeFile(root / "other.md", "Kalman without tags, #coursework\n");
    makeTaggedPdf(root / "paper.pdf", "course/physics");
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    auto names = [&] {
        std::vector<std::string> out;
        for (int i = 0; i < model.rowCount(); ++i) {
            out.push_back(fs::path(model.data(model.index(i), LibraryModel::PathRole).toString().toStdString())
                                  .filename()
                                  .string());
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    model.setFuzzySearch(true);
    model.setSearchQuery("tag:course");
    EXPECT_EQ(names(), (std::vector<std::string>{"draft.md", "kalman.md", "paper.pdf"}));
    model.setSearchQuery("tag:course/");
    EXPECT_EQ(names(), (std::vector<std::string>{"kalman.md", "paper.pdf"}));
    model.setSearchQuery("kalman !tag:draft");
    EXPECT_EQ(names(), (std::vector<std::string>{"kalman.md", "other.md"}));
    model.setSearchQuery("tag:course | tag:coursework");
    EXPECT_EQ(names().size(), 4u);
    model.setNamesOnly(true);
    model.setSearchQuery("tag:draft");
    EXPECT_EQ(names(), (std::vector<std::string>{"draft.md"}));
    model.setNamesOnly(false);
    // The plain search: the tag terms filter, the rest is searched
    model.setFuzzySearch(false);
    model.setSearchQuery("tag:course");
    EXPECT_EQ(names(), (std::vector<std::string>{"draft.md", "kalman.md", "paper.pdf"}));
    model.setSearchQuery("draft tag:course");
    EXPECT_EQ(names(), (std::vector<std::string>{"draft.md"}));
    model.setSearchQuery("kalman TAG:Course/Math");
    EXPECT_EQ(names(), (std::vector<std::string>{"kalman.md"}));
    model.setNamesOnly(true);
    model.setSearchQuery("tag:course/physics");
    EXPECT_EQ(names(), (std::vector<std::string>{"paper.pdf"}));
}

// Tags written into a PDF's keywords: an incremental update (the file as it was stays its start), the document
// information and, where there is one, the XMP's dc:subject; keywords that are still wanted keep their spelling
TEST(Tags, writtenIntoAPdfsKeywords) {
    EXPECT_EQ(pdfkeywords::keywordsFor("Machine learning, optics; C++", list({"optics", "Machine-learning", "exam"})),
              "Machine learning, optics, exam");
    EXPECT_EQ(pdfkeywords::keywordsFor("", list({"a", "A", "b"})), "a, b");
    QTemporaryDir tmp;
    const fs::path dir(tmp.path().toStdString());
    makeTaggedPdf(dir / "paper.pdf", "Machine learning, physics", {"physics"});
    std::string before;
    {
        std::ifstream in(dir / "paper.pdf", std::ios::binary);
        before.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::string error;
    ASSERT_TRUE(pdfkeywords::write(dir / "paper.pdf", list({"Machine-learning", "exam", "course/math"}), error)) << error;
    const auto k = pdfkeywords::read(dir / "paper.pdf");
    EXPECT_EQ(k.info, "Machine learning, exam, course/math");
    EXPECT_EQ(k.subject, list({"Machine-learning", "exam", "course/math"}));
    std::string after;
    {
        std::ifstream in(dir / "paper.pdf", std::ios::binary);
        after.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    EXPECT_EQ(after.substr(0, before.size()), before) << "appended, the file as it was stays";
    {
        QPDF q;
        q.processFile((dir / "paper.pdf").string().c_str());
        EXPECT_EQ(q.getAllPages().size(), 1u);
    }
    // None: the keywords go, also from the XMP
    ASSERT_TRUE(pdfkeywords::write(dir / "paper.pdf", {}, error)) << error;
    EXPECT_EQ(pdfkeywords::tagsOf(dir / "paper.pdf"), QStringList());
    // A PDF without document information gets one
    makeTaggedPdf(dir / "bare.pdf", "");
    ASSERT_TRUE(pdfkeywords::write(dir / "bare.pdf", list({"one"}), error)) << error;
    EXPECT_EQ(pdfkeywords::tagsOf(dir / "bare.pdf"), list({"one"}));
    writeFile(dir / "broken.pdf", "%PDF-1.4 nothing");
    EXPECT_FALSE(pdfkeywords::write(dir / "broken.pdf", list({"x"}), error));
    EXPECT_FALSE(error.empty());
}

// A PDF with notes keeps its notes and its keywords through the app's saves (in full and incremental); an archive PDF
// stays PDF/A (its XMP follows the document information)
TEST(Tags, pdfsWithNotesAndArchivesKeepTheirKeywords) {
    QTemporaryDir tmp;
    const fs::path dir(tmp.path().toStdString());
    auto makeDoc = [] {
        auto doc = std::make_unique<Document>(nullptr);
        auto page = std::make_shared<XojPage>(595.0, 842.0);
        page->getLayers().push_back(new Layer());
        page->getLayers().front()->addElement(textAt("Notes #typed", false));
        doc->addPage(page);
        return doc;
    };
    const fs::path notes = dir / "lecture.pdf";
    {
        auto doc = makeDoc();
        ASSERT_TRUE(HybridPdf::write(*doc, notes).ok);
    }
    std::string error;
    ASSERT_TRUE(pdfkeywords::write(notes, list({"exam"}), error)) << error;
    {
        auto loaded = DocumentSession::loadFile(notes);
        ASSERT_TRUE(loaded.document) << loaded.error;
        EXPECT_TRUE(loaded.hybrid);
        EXPECT_TRUE(loaded.hybridChanged.empty()) << "our annotations are as we wrote them";
        // Saved again incrementally, then in full
        auto opened = HybridPdf::open(notes);
        ASSERT_TRUE(opened.document) << opened.error;
        const HybridPdf::Revision rev = HybridPdf::revisionOf(opened.base, notes);
        HybridPdf::WriteOptions incremental;
        incremental.revision = &rev;
        ASSERT_TRUE(HybridPdf::write(*opened.document, notes, {}, npos, {}, incremental).ok);
        EXPECT_EQ(pdfkeywords::tagsOf(notes), list({"exam"})) << "after an incremental save";
        HybridPdf::WriteOptions full;
        full.compact = true;
        auto again = HybridPdf::open(notes);
        ASSERT_TRUE(again.document) << again.error;
        ASSERT_TRUE(HybridPdf::write(*again.document, notes, {}, npos, {}, full).ok);
        EXPECT_EQ(pdfkeywords::tagsOf(notes), list({"exam"})) << "after a full save";
    }
    // An archive PDF
    const fs::path archive = dir / "lecture.archive.pdf";
    {
        auto doc = makeDoc();
        ASSERT_TRUE(HybridPdf::writeArchive(*doc, archive).ok);
    }
    ASSERT_TRUE(pdfkeywords::write(archive, list({"kept", "exam"}), error)) << error;
    EXPECT_EQ(pdfkeywords::read(archive).info, "kept, exam");
    QPDF q;
    q.processFile(archive.string().c_str());
    auto buffer = q.getRoot().getKey("/Metadata").getStreamData(qpdf_dl_all);
    const std::string xmp(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
    EXPECT_NE(xmp.find("<pdf:Keywords>kept, exam</pdf:Keywords>"), std::string::npos);
    EXPECT_NE(xmp.find("<pdfaid:part>3</pdfaid:part>"), std::string::npos) << "still PDF/A";
    EXPECT_TRUE(HybridPdf::isArchive(archive));
}
