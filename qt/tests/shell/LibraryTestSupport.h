/*
 * xournal-qt: what the library's tests share (LibraryTest, LibrarySearchTest, LibraryCoversTest,
 * LibraryPlacesTest, LibraryIndexTest, LibraryPacksTest): the fixture `LibraryTest` (a temporary library folder) and
 * the helpers that make documents in it.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <fstream>
#include <iostream>
#include <array>
#include <map>
#include <random>

#include <fcntl.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <gtest/gtest.h>

#include <cairo-pdf.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/PathUtil.h"
#include "session/DocumentSession.h"
#include "shell/DocumentFiles.h"
#include "shell/DocumentPlaces.h"
#include "shell/HitPages.h"
#include "shell/Library.h"
#include "shell/LibraryIndex.h"
#include "shell/LibraryCache.h"
#include "shell/LibraryModel.h"
#include "shell/DocumentCovers.h"
#include "shell/RecentFiles.h"

#include "session/DocumentSearch.h"
#include "session/DocumentTextIndex.h"
#include "shell/TabManager.h"
#include "AppController.h"
#include "config-test.h"
#include "support/TestSupport.h"

using xqt::test::fixture;
using xqt::test::waitFor;
using xqt::test::processEventsFor;

using namespace xqt;

namespace xqt::test::library {

inline void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

/// A PDF with text ("xournal" on page 1, "Page 2" on page 2), copied from the fixtures.
inline void makePdf(const fs::path& p) {
    fs::create_directories(p.parent_path());
    fs::copy_file(fixture(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf"), p);
}

/// "<name>.xopp" annotating `pdf` (a relative reference, as upstream saves it).
inline void makeAnnotation(const fs::path& pdf, const fs::path& xopp) {
    auto loaded = DocumentSession::loadFile(pdf);
    ASSERT_TRUE(loaded.document) << loaded.error;
    ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, xopp).ok);
}

/// The background PDF the .xopp loads, empty if it has none or it is missing.
inline fs::path backgroundOf(const fs::path& xopp) {
    auto loaded = DocumentSession::loadFile(xopp);
    EXPECT_TRUE(loaded.document) << loaded.error;
    if (!loaded.document || !loaded.missingPdf.empty() || loaded.attachedPdfMissing) {
        return {};
    }
    return loaded.document->getPdfFilepath();
}

/// A one-page PDF with `word` on it.
inline void makeWordPdf(const fs::path& p, const char* word) {
    fs::create_directories(p.parent_path());
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    cairo_move_to(cr, 72, 100);
    cairo_show_text(cr, word);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}
/// Add a text element to a page of a .xopp and save it.
inline void addText(const fs::path& xopp, size_t page, const char* text) {
    auto loaded = DocumentSession::loadFile(xopp);
    ASSERT_TRUE(loaded.document);
    auto t = std::make_unique<Text>();
    t->setText(text);
    t->move(100, 100);
    loaded.document->getPage(page)->getSelectedLayer()->addElement(std::move(t));
    ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, xopp).ok);
}

inline std::vector<std::string> names(const std::vector<DocumentItem>& items) {
    std::vector<std::string> n;
    for (const auto& i: items) {
        n.push_back(i.name());
    }
    return n;
}

/// The keys of a pack in a folder's cache (sorted).
inline QStringList packKeys(const fs::path& folder, const QString& pack, int format = LibraryIndex::FORMAT) {
    QStringList keys;
    if (auto entries = Packs::read(folder / DocumentFiles::META_DIR, pack, format)) {
        for (auto it = entries->cbegin(); it != entries->cend(); ++it) {
            keys << it.key().toString();
        }
    }
    keys.sort();
    return keys;
}
}  // namespace xqt::test::library

using namespace xqt::test::library;

/// One fixture for all the library's test files (one suite "LibraryTest": a type of its own in each file would make
/// gtest refuse the suite).
class LibraryTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
    }
    QTemporaryDir tmp;
    fs::path root;
};
