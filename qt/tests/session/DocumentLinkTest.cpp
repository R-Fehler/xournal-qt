/*
 * xournal-qt: the link format between documents (qt/docs/links.md): parsing, writing, relative paths, and where a
 * link leads (chapter by title, then normalised title, then the saved page; the PDF page; the page's fingerprint;
 * a .md heading by its slug, then its line).
 *
 * @license GNU GPLv2 or later
 */
#include <gtest/gtest.h>

#include "session/DocumentLink.h"

using namespace xqt;
using links::Link;

namespace {
Link make(QString path, int page = 0, int pdfPage = 0, QString chapter = {}, QString heading = {}, int line = 0,
          QString text = {}) {
    Link l;
    l.path = std::move(path);
    l.page = page;
    l.pdfPage = pdfPage;
    l.chapter = std::move(chapter);
    l.heading = std::move(heading);
    l.line = line;
    l.text = std::move(text);
    return l;
}
}  // namespace

TEST(DocumentLinkTest, parsesTheFormsOfTheDesign) {
    struct Case {
        const char* target;
        std::optional<Link> expected;
    };
    const Case cases[] = {
            {"../Lectures/Kalman%20filter.xopp#page=12", make("../Lectures/Kalman filter.xopp", 12)},
            {"../Lectures/Kalman filter.pdf#page=12", make("../Lectures/Kalman filter.pdf", 12)},
            {"../Lectures/Kalman%20filter.xopp#chapter=Prediction%20step&page=12",
             make("../Lectures/Kalman filter.xopp", 12, 0, "Prediction step")},
            {"lecture.xopp#page=12&pdfpage=7", make("lecture.xopp", 12, 7)},
            {"notes.xopp#page=3&text=kalman%20filter%20prediction", make("notes.xopp", 3, 0, {}, {}, 0,
                                                                          "kalman filter prediction")},
            {"../Notes/turbines.md#heading=blade-design&line=40", make("../Notes/turbines.md", 0, 0, {}, "blade-design", 40)},
            // A plain fragment of a .md is a heading (GitHub, Obsidian); of another document a chapter
            {"note.md#blade-design", make("note.md", 0, 0, {}, "blade-design")},
            {"note.md#Blade%20design", make("note.md", 0, 0, {}, "Blade design")},
            {"lecture.pdf#Introduction", make("lecture.pdf", 0, 0, "Introduction")},
            // Unknown keys (the PDF open parameters have more) are left out
            {"paper.pdf#page=4&zoom=200", make("paper.pdf", 4)},
            {"paper.pdf", make("paper.pdf")},
            {"<my notes.xopp>", make("my notes.xopp")},
            {"/home/me/Lectures/a.xopp#page=2", make("/home/me/Lectures/a.xopp", 2)},
            {"file:///home/me/Lectures/a%20b.xopp#page=2", make("/home/me/Lectures/a b.xopp", 2)},
            // The same document
            {"#page=5", make("", 5)},
            {"#chapter=Intro", make("", 0, 0, "Intro")},
            // Not links to documents
            {"https://example.org/a.pdf#page=2", std::nullopt},
            {"mailto:me@example.org", std::nullopt},
            {"#Page:12", std::nullopt},
            {"", std::nullopt},
            {"#", std::nullopt},
            {"#heading-in-this-text", std::nullopt},
            {"example.org/page", std::nullopt},
            {"javascript:alert(1)", std::nullopt},
    };
    for (const Case& c: cases) {
        const auto parsed = links::parse(QString::fromUtf8(c.target));
        ASSERT_EQ(parsed.has_value(), c.expected.has_value()) << c.target;
        if (parsed) {
            EXPECT_EQ(parsed->path, c.expected->path) << c.target;
            EXPECT_EQ(parsed->page, c.expected->page) << c.target;
            EXPECT_EQ(parsed->pdfPage, c.expected->pdfPage) << c.target;
            EXPECT_EQ(parsed->chapter, c.expected->chapter) << c.target;
            EXPECT_EQ(parsed->heading, c.expected->heading) << c.target;
            EXPECT_EQ(parsed->line, c.expected->line) << c.target;
            EXPECT_EQ(parsed->text, c.expected->text) << c.target;
            EXPECT_FALSE(parsed->wiki);
        }
    }
}

TEST(DocumentLinkTest, readsWikiLinks) {
    const auto a = links::parseWiki("turbines#Blade design");
    ASSERT_TRUE(a);
    EXPECT_EQ(a->path, "turbines");
    EXPECT_EQ(a->heading, "Blade design");
    EXPECT_TRUE(a->wiki);
    const auto b = links::parseWiki("Lectures/Kalman");
    ASSERT_TRUE(b);
    EXPECT_EQ(b->path, "Lectures/Kalman");
    EXPECT_TRUE(b->heading.isEmpty());
    EXPECT_FALSE(links::parseWiki(""));
    EXPECT_FALSE(links::parseWiki("#only a heading"));
}

TEST(DocumentLinkTest, writesWhatItReadsAgain) {
    struct Case {
        Link link;
        const char* written;
    };
    const Case cases[] = {
            {make("../Lectures/Kalman filter.xopp", 12), "../Lectures/Kalman%20filter.xopp#page=12"},
            {make("../Lectures/Kalman filter.xopp", 12, 0, "Prediction step"),
             "../Lectures/Kalman%20filter.xopp#chapter=Prediction%20step&page=12"},
            {make("lecture.xopp", 12, 7), "lecture.xopp#page=12&pdfpage=7"},
            {make("../Notes/turbines.md", 0, 0, {}, "blade-design", 40), "../Notes/turbines.md#heading=blade-design&line=40"},
            {make("a (draft) #2.xopp"), "a%20%28draft%29%20%232.xopp"},
            {make("x.xopp", 3, 0, "A & B = C"), "x.xopp#chapter=A%20%26%20B%20%3D%20C&page=3"},
            {make("x.xopp", 3, 0, {}, {}, 0, "kalman filter"), "x.xopp#page=3&text=kalman%20filter"},
            {make("Übung 3.xopp", 1), "Übung%203.xopp#page=1"},
    };
    for (const Case& c: cases) {
        const QString written = links::write(c.link);
        EXPECT_EQ(written, QString::fromUtf8(c.written));
        const auto again = links::parse(written);
        ASSERT_TRUE(again) << c.written;
        EXPECT_EQ(*again, c.link) << c.written;
    }
    EXPECT_EQ(links::markdown("Kalman [draft]", make("k.xopp", 2)), "[Kalman \\[draft\\]](k.xopp#page=2)");
    EXPECT_EQ(links::markdown("", make("../Lectures/k.xopp", 2)), "[k.xopp](../Lectures/k.xopp#page=2)");
}

TEST(DocumentLinkTest, pathsAreRelativeToTheDocumentHoldingTheLink) {
    const fs::path root = fs::path("/lib");
    EXPECT_EQ(links::relativePath(root / "Notes" / "a.xopp", root / "Lectures" / "Kalman filter.xopp"),
              "../Lectures/Kalman filter.xopp");
    EXPECT_EQ(links::relativePath(root / "a.xopp", root / "b.md"), "b.md");
    EXPECT_EQ(links::relativePath(root / "a.xopp", root / "Sub" / "c.pdf"), "Sub/c.pdf");
    EXPECT_EQ(links::resolvePath(root / "Notes", "../Lectures/Kalman filter.xopp"),
              root / "Lectures" / "Kalman filter.xopp");
    EXPECT_EQ(links::resolvePath(root / "Notes", "/abs/x.pdf"), fs::path("/abs/x.pdf"));
}

TEST(DocumentLinkTest, slugsAndNormalisedTitles) {
    struct Case {
        const char* in;
        const char* slug;
        const char* normalised;
    };
    const Case cases[] = {
            {"Blade design", "blade-design", "blade design"},
            {"blade-design", "blade-design", "blade design"},
            {"Blade design (v2)", "blade-design-v2", "blade design v2"},
            {"  Prediction   step:  ", "prediction-step", "prediction step"},
            {"Über Wärme", "über-wärme", "über wärme"},
            {"snake_case name", "snake_case-name", "snake case name"},
            {"1.2 The Kalman-Filter!", "12-the-kalman-filter", "1 2 the kalman filter"},
    };
    for (const Case& c: cases) {
        EXPECT_EQ(links::slug(QString::fromUtf8(c.in)), QString::fromUtf8(c.slug)) << c.in;
        EXPECT_EQ(links::normalised(QString::fromUtf8(c.in)), QString::fromUtf8(c.normalised)) << c.in;
    }
    EXPECT_EQ(links::fingerprint("  # Kalman **filter**, the prediction step and more words  "),
              "kalman filter the prediction step");
    EXPECT_EQ(links::fingerprint(""), "");
    EXPECT_LE(links::fingerprint("Supercalifragilistic expialidocious antidisestablishmentarianism "
                                 "pneumonoultramicroscopic silicovolcanoconiosis")
                      .size(),
              48);
}

TEST(DocumentLinkTest, resolvesByChapterThenPageWithANote) {
    const std::vector<links::Chapter> chapters{{"Introduction", 0}, {"Prediction step", 11}, {"Update: the gain", 20}};
    const std::vector<links::Page> pages(30);
    struct Case {
        Link link;
        int page;
        const char* note;
    };
    const Case cases[] = {
            {make("k.xopp", 12, 0, "Prediction step"), 11, ""},
            {make("k.xopp", 5, 0, "prediction STEP"), 11, ""},         // case
            {make("k.xopp", 5, 0, "Update - the gain!"), 20, ""},      // punctuation
            {make("k.xopp", 12, 0, "Correction"), 11, "Chapter \"Correction\" not found, opened page 12"},
            {make("k.xopp", 0, 0, "Correction"), 0, "Chapter \"Correction\" not found"},
            {make("k.xopp", 99), 29, "Page 99 not found, opened page 30"},  // the last page
            {make("k.xopp", 3), 2, ""},
            {make("k.xopp"), 0, ""},
    };
    for (const Case& c: cases) {
        const links::Place p = links::resolve(c.link, chapters, pages);
        EXPECT_EQ(p.page, c.page) << links::write(c.link).toStdString();
        EXPECT_EQ(p.note, QString::fromUtf8(c.note)) << links::write(c.link).toStdString();
    }
}

TEST(DocumentLinkTest, resolvesByPdfPageThenByFingerprint) {
    // A lecture: PDF pages 1-5, a notes page inserted after PDF page 2, and one more before the first
    std::vector<links::Page> pages{{0, "My notes on the lecture"}, {1, ""}, {2, ""}, {0, "Kalman filter prediction step"},
                                   {3, ""},                          {4, ""}, {5, ""}};
    struct Case {
        Link link;
        int page;
        const char* note;
    };
    const Case cases[] = {
            // PDF page 3 was page 3 when the link was made; two pages came before it
            {make("l.xopp", 3, 3), 4, ""},
            {make("l.xopp", 1, 9), 0, "PDF page 9 not found, opened page 1"},
            // The fingerprint: page 3 was the notes page; it is page 4 now
            {make("l.xopp", 3, 0, {}, {}, 0, "kalman filter prediction"), 3, ""},
            {make("l.xopp", 4, 0, {}, {}, 0, "kalman filter prediction"), 3, ""},
            {make("l.xopp", 7, 0, {}, {}, 0, "my notes on the"), 0, ""},
            // No page has it any more: the page number
            {make("l.xopp", 6, 0, {}, {}, 0, "gone text"), 5, ""},
    };
    for (const Case& c: cases) {
        const links::Place p = links::resolve(c.link, {}, pages);
        EXPECT_EQ(p.page, c.page) << links::write(c.link).toStdString();
        EXPECT_EQ(p.note, QString::fromUtf8(c.note)) << links::write(c.link).toStdString();
    }
    // The nearest page with the text wins
    std::vector<links::Page> twice{{0, "same"}, {0, "other"}, {0, "other"}, {0, "other"}, {0, "same"}};
    EXPECT_EQ(links::resolve(make("t.xopp", 4, 0, {}, {}, 0, "same"), {}, twice).page, 4);
    EXPECT_EQ(links::resolve(make("t.xopp", 2, 0, {}, {}, 0, "same"), {}, twice).page, 0);
}

TEST(DocumentLinkTest, resolvesHeadingsAndLinesOfMarkdown) {
    const std::string text = "# Turbines\n\nIntro text.\n\n## Blade design\n\nBlades.\n\n### Tip (v2)\n\nline 11\n";
    struct Case {
        Link link;
        size_t offset;
        const char* note;
    };
    const size_t blade = text.find("## Blade design");
    const size_t tip = text.find("### Tip");
    const size_t line11 = text.find("line 11");
    const Case cases[] = {
            {make("t.md", 0, 0, {}, "blade-design"), blade, ""},
            {make("t.md", 0, 0, {}, "Blade design"), blade, ""},  // Obsidian writes the heading itself
            {make("t.md", 0, 0, {}, "tip-v2"), tip, ""},
            {make("t.md", 0, 0, {}, "gone", 11), line11, "Heading \"gone\" not found, opened line 11"},
            {make("t.md", 0, 0, {}, "gone"), 0, "Heading \"gone\" not found"},
            {make("t.md", 0, 0, {}, {}, 5), blade, ""},
            {make("t.md", 0, 0, {}, {}, 500), text.size(), ""},  // after the end: the end
            {make("t.md"), 0, ""},
    };
    for (const Case& c: cases) {
        const links::TextPlace p = links::resolveInText(c.link, text);
        EXPECT_EQ(p.offset, c.offset) << links::write(c.link).toStdString();
        EXPECT_EQ(p.note, QString::fromUtf8(c.note)) << links::write(c.link).toStdString();
    }
}
