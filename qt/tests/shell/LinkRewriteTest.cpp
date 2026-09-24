/*
 * xournal-qt: links between documents kept working (qt/docs/links.md): the library index records the links of
 * notes and Markdown files, backlinks come from them, and after a rename or move the links that point elsewhere now
 * are written anew - in the text of a Markdown file byte for byte, in the Markdown boxes of a .xopp.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <memory>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/PageType.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/DocumentFiles.h"
#include "shell/DocumentLinks.h"
#include "shell/Library.h"
#include "shell/LinkRewrite.h"

using namespace xqt;

namespace {
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void writeFile(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << bytes;
}

/// A .xopp with one page whose Markdown layer holds `markdown` (a box, or a link marker).
void makeNotes(const fs::path& file, const std::string& markdown) {
    fs::create_directories(file.parent_path());
    Document doc(nullptr);
    auto page = std::make_shared<XojPage>(595.0, 842.0);
    auto* layer = new Layer();
    layer->setName(std::string(xoj::markdown::LAYER_NAME));
    page->getLayers().push_back(layer);  // (the page owns it)
    page->getLayers().push_back(new Layer());
    auto text = std::make_unique<Text>();
    text->setText(markdown);
    text->setFont(XojFont("Sans", 10));
    text->setWrap(300);
    layer->addElement(std::move(text));
    doc.addPage(page);
    ASSERT_TRUE(DocumentSession::writeDocument(doc, file).ok);
}

/// The texts of a .xopp's text elements, joined.
std::string textsOf(const fs::path& xopp) {
    auto loaded = DocumentSession::loadFile(xopp);
    EXPECT_TRUE(loaded.document) << loaded.error;
    std::string all;
    for (size_t i = 0; loaded.document && i < loaded.document->getPageCount(); ++i) {
        for (const Layer* l: loaded.document->getPage(i)->getLayers()) {
            for (const Element* e: l->getElementsView()) {
                if (e->getType() == ELEMENT_TEXT) {
                    all += static_cast<const Text*>(e)->getText();
                }
            }
        }
    }
    return all;
}
}  // namespace

TEST(LinkRewrite, plansTheLinksThatPointElsewhereAfterAMove) {
    QTemporaryDir tmp;
    const fs::path root(tmp.path().toStdString());
    // After the moves (the files are where they went; the index may still know the old paths)
    writeFile(root / "Lectures" / "Kalman filter.xopp", "x");
    writeFile(root / "Notes" / "Sub" / "a.md", "x");
    writeFile(root / "Notes" / "b.md", "x");
    const LinkRewrite::Moves moves{{root / "Lectures" / "kalman.xopp", root / "Lectures" / "Kalman filter.xopp"},
                                   {root / "Notes" / "a.md", root / "Notes" / "Sub" / "a.md"}};
    const std::vector<LinkRewrite::Source> sources{
            // b.md links to the renamed lecture (by path and by name) and to a.md, which moved
            {root / "Notes" / "b.md",
             {"../Lectures/kalman.xopp#chapter=Prediction%20step&page=4", "a.md", "../Lectures/other.xopp#page=2",
              "https://example.org"},
             {"kalman#Prediction", "unrelated"}},
            // a.md moved (the index still has its old path): its own relative links change
            {root / "Notes" / "a.md", {"b.md#heading=x", "../Lectures/kalman.xopp"}, {}},
    };
    const auto plans = LinkRewrite::plan(sources, moves);
    ASSERT_EQ(plans.size(), 2u);
    EXPECT_EQ(plans[0].file, root / "Notes" / "b.md");
    ASSERT_EQ(plans[0].changes.size(), 3u);
    EXPECT_EQ(plans[0].changes[0].from, "../Lectures/kalman.xopp#chapter=Prediction%20step&page=4");
    EXPECT_EQ(plans[0].changes[0].to, "../Lectures/Kalman%20filter.xopp#chapter=Prediction%20step&page=4");
    EXPECT_EQ(plans[0].changes[1].from, "a.md");
    EXPECT_EQ(plans[0].changes[1].to, "Sub/a.md");
    EXPECT_TRUE(plans[0].changes[2].wiki);
    EXPECT_EQ(plans[0].changes[2].to, "Kalman filter#Prediction");
    EXPECT_EQ(plans[1].file, root / "Notes" / "Sub" / "a.md");
    ASSERT_EQ(plans[1].changes.size(), 2u);
    EXPECT_EQ(plans[1].changes[0].to, "../b.md#heading=x");
    EXPECT_EQ(plans[1].changes[1].to, "../../Lectures/Kalman%20filter.xopp");

    // A folder moved: links into it and out of it
    const LinkRewrite::Moves folder{{root / "Old", root / "Notes" / "Sub"}};
    const auto plans2 =
            LinkRewrite::plan({{root / "Notes" / "b.md", {"../Old/a.md#line=3"}, {}}}, folder);
    ASSERT_EQ(plans2.size(), 1u);
    EXPECT_EQ(plans2[0].changes[0].to, "Sub/a.md#line=3");
    EXPECT_TRUE(LinkRewrite::plan(sources, {}).empty());
}

TEST(LinkRewrite, rewritesOnlyTheLinkTargetsOfAMarkdownText) {
    std::string text =
            "See [the lecture](../Lectures/kalman.xopp#page=4) and <../Lectures/kalman.xopp> as text,\n"
            "[again](<../Lectures/kalman.xopp#page=4> \"title\"), [[kalman#Prediction|the step]], [[kalmanx]],\n"
            "[ref]: ../Lectures/kalman.xopp#page=4\n"
            "not a link: ../Lectures/kalman.xopp#page=4\n";
    const std::vector<LinkRewrite::Change> changes{
            {"../Lectures/kalman.xopp#page=4", "../Lectures/Kalman%20filter.xopp#page=4", false},
            {"kalman#Prediction", "Kalman filter#Prediction", true}};
    EXPECT_EQ(LinkRewrite::rewriteMarkdown(text, changes), 4);
    EXPECT_EQ(text,
              "See [the lecture](../Lectures/Kalman%20filter.xopp#page=4) and <../Lectures/kalman.xopp> as text,\n"
              "[again](<../Lectures/Kalman%20filter.xopp#page=4> \"title\"), [[Kalman filter#Prediction|the step]], "
              "[[kalmanx]],\n"
              "[ref]: ../Lectures/Kalman%20filter.xopp#page=4\n"
              "not a link: ../Lectures/kalman.xopp#page=4\n");
}

TEST(LinkRewrite, filesAreWrittenAgainByteForByteAndNotesThroughTheirBoxes) {
    QTemporaryDir tmp;
    const fs::path root(tmp.path().toStdString());
    const std::string md = "\xEF\xBB\xBF# Notes\r\n\r\nSee [k](../L/k.xopp#page=2).\r\nLast line without end";
    writeFile(root / "N" / "a.md", md);
    makeNotes(root / "N" / "sketch.xopp", "[\xF0\x9F\x94\x97 k, page 2](../L/k.xopp#page=2)");
    const std::vector<LinkRewrite::Change> changes{{"../L/k.xopp#page=2", "../L/K2.xopp#page=2", false}};
    std::string error;
    EXPECT_EQ(LinkRewrite::rewriteFile(root / "N" / "a.md", changes, error), 1) << error;
    EXPECT_EQ(readFile(root / "N" / "a.md"),
              "\xEF\xBB\xBF# Notes\r\n\r\nSee [k](../L/K2.xopp#page=2).\r\nLast line without end");
    EXPECT_EQ(LinkRewrite::rewriteFile(root / "N" / "sketch.xopp", changes, error), 1) << error;
    EXPECT_EQ(textsOf(root / "N" / "sketch.xopp"), "[\xF0\x9F\x94\x97 k, page 2](../L/K2.xopp#page=2)");
    EXPECT_EQ(LinkRewrite::rewriteFile(root / "N" / "a.md", changes, error), 0) << "nothing left to change";
}

TEST(LinkRewrite, theIndexKnowsTheLinksOfNotesAndMarkdownFilesAndTheBacklinks) {
    QTemporaryDir tmp;
    const fs::path root(tmp.path().toStdString());
    makeNotes(root / "Lectures" / "kalman.xopp", "# Kalman\n\nSee [the notes](../Notes/a.md#heading=x).");
    makeNotes(root / "Notes" / "sketch.xopp", "[\xF0\x9F\x94\x97 kalman, page 1](../Lectures/kalman.xopp#page=1)");
    writeFile(root / "Notes" / "a.md", "Read [[kalman#Kalman]] and [b](b.md).\n");
    writeFile(root / "Notes" / "b.md", "Nothing.\n");
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    const auto sources = index.linkSources();
    ASSERT_EQ(sources.size(), 3u);
    const auto kalmanFrom = DocumentLinks::backlinks(sources, root / "Lectures" / "kalman.xopp");
    ASSERT_EQ(kalmanFrom.size(), 2u);
    EXPECT_NE(std::find(kalmanFrom.begin(), kalmanFrom.end(), root / "Notes" / "sketch.xopp"), kalmanFrom.end());
    EXPECT_NE(std::find(kalmanFrom.begin(), kalmanFrom.end(), root / "Notes" / "a.md"), kalmanFrom.end())
            << "by its wiki link";
    EXPECT_EQ(DocumentLinks::backlinks(sources, root / "Notes" / "a.md"),
              std::vector<fs::path>{root / "Lectures" / "kalman.xopp"});
    EXPECT_EQ(DocumentLinks::backlinks(sources, root / "Notes" / "b.md"), std::vector<fs::path>{root / "Notes" / "a.md"});

    // The link of a file that is gone: found by its name, else by the page's text
    links::Link gone;
    gone.path = "../Old/kalman.xopp";
    EXPECT_EQ(DocumentLinks::findMoved(gone, root / "Notes" / "a.md", index), root / "Lectures" / "kalman.xopp");
    links::Link renamed;
    renamed.path = "../Lectures/k-renamed.xopp";
    renamed.page = 1;
    renamed.text = "kalman see the notes";
    EXPECT_EQ(DocumentLinks::findMoved(renamed, root / "Notes" / "a.md", index), root / "Lectures" / "kalman.xopp");
    renamed.text = "nowhere to be found";
    EXPECT_TRUE(DocumentLinks::findMoved(renamed, root / "Notes" / "a.md", index).empty());

    // Stored in the packs: opened again, nothing is read
    index.flush();
    LibraryIndex again(root);
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 0);
    EXPECT_EQ(again.linkSources().size(), 3u);
}
