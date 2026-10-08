/*
 * xournal-qt-cli: headless command line tool built on the Qt-free Xournal++ core (xoj-core).
 *
 * The export options mirror upstream `xournalpp` exactly (same names, same semantics, same exit codes: -2 = load
 * failure, -3 = export/save failure), so both binaries can be driven identically by the golden-image tests.
 * Additional fork options:
 *   --resave=OUT.xopp      load a .xopp/.xoj and save it again (round-trip test)
 *   --dump                 print a structural summary of the document (pages, layers, elements)
 *   --bench-render=ZOOM    render every page at ZOOM and print timings
 *   --pdf-dir=DIR          export every FILE as DIR/<name>.pdf (many documents in one go)
 *   --png-dir=DIR          export the pages of every FILE as pictures DIR/<name>-p001.png, ... (as the app names
 *                          them, qt/docs/features/page-files.md; --export-range, --export-png-dpi, default 300 as in
 *                          the app)
 * and commands:
 *   hwr-lines DOC --out DIR [--text FILE] [--lang de] [--writer ID] [--licence ID]
 *                          the handwriting of DOC as a line dataset (qt/src/hwr/LineDataset.h; this command links Qt)
 *   hwr-form FILLED --out DIR [--manifest FILE] [--writer ID] [--licence ID]
 *                          a filled handwriting form as a line dataset (qt/src/hwr/FormDataset.h)
 *   hwr-bench FILLED [--model DIR]... [--manifest FILE] [--out REPORT]
 *                          a filled handwriting form as a benchmark of the handwriting search (qt/src/hwr/FormBench.h)
 *   export-xopp PDF [--version N] [-o OUT.xopp]
 *                          the .xopp of a PDF with notes, of its latest or any version (version history,
 *                          qt/src/session/PdfHistory.h; links Qt)
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <array>
#include <chrono>
#include <clocale>
#include <cstdlib>
#include <iostream>
#include <locale>
#include <memory>
#include <string>

#include <cairo.h>
#include <glib.h>

#include "control/ExportHelper.h"
#include "control/pagetype/PageTypeHandler.h"
#include "control/jobs/ExportBackgroundType.h"
#include "control/xojfile/LoadHandler.h"
#include "control/xojfile/SaveHandler.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "pdf/base/PdfExportBackend.h"
#include "util/ElementRange.h"
#include "util/PathUtil.h"
#include "util/PlaceholderString.h"
#include "util/VersionInfo.h"
#include "util/i18n.h"
#include "util/raii/CStringWrapper.h"
#include "view/DocumentView.h"

#include "filesystem.h"

#ifdef _WIN32
#include "../src/app/WindowsFonts.h"
#endif
#ifdef XQT_CLI_HWR
#include <QCoreApplication>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include "hwr/FormBench.h"
#include "hwr/FormDataset.h"
#include "hwr/FormManifest.h"
#include "hwr/HandwritingSearch.h"
#include "hwr/LineDataset.h"
#include "hwr/ModelInfo.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#ifdef XQT_HWR_ONNX
#include "hwr/HwrInfo.h"
#endif
#endif
#ifdef XQT_CLI_SESSION
#include "render/PaperTexture.h"
#include "session/PdfHistory.h"
#endif

namespace {

void throwIfMissingPdfFileName(const LoadHandler& loader) {
    if (!loader.getMissingPdfFilename().empty()) {
        throw std::runtime_error{
                FS(_F("The background file \"{1}\" could not be found. It might have been moved, renamed or deleted.") %
                   loader.getMissingPdfFilename().u8string())};
    }
}

auto loadDocumentOrExit(const fs::path& filename, ExportBackgroundType exportBackground) -> std::unique_ptr<Document> {
    try {
        LoadHandler loader;
        auto doc = loader.loadDocument(filename);
        if (exportBackground != EXPORT_BACKGROUND_NONE) {
            throwIfMissingPdfFileName(loader);
        }
        return doc;
    } catch (const std::exception& e) {
        std::cerr << FS(_F("Error loading document: {1}") % e.what()) << std::endl;
        std::exit(-2);
    }
}

int exportImg(const fs::path& infile, const fs::path& outfile, const char* range, const char* layerRange, int pngDpi,
              int pngWidth, int pngHeight, ExportBackgroundType exportBackground) {
    auto doc = loadDocumentOrExit(infile, exportBackground);
    try {
        ExportHelper::exportImg(doc.get(), outfile, range, layerRange, pngDpi, pngWidth, pngHeight, exportBackground);
    } catch (const std::exception& e) {
        std::cerr << FS(_F("Error exporting image: {1}") % e.what()) << std::endl;
        std::exit(-3);
    }
    return 0;
}

int exportPdf(const fs::path& infile, const fs::path& outfile, const char* range, const char* layerRange,
              ExportBackgroundType exportBackground, bool progressiveMode, ExportBackend backend) {
    auto doc = loadDocumentOrExit(infile, exportBackground);
    try {
        ExportHelper::exportPdf(doc.get(), outfile, range, layerRange, exportBackground, progressiveMode, backend);
    } catch (const std::exception& e) {
        std::cerr << FS(_F("Error exporting PDF: {1}") % e.what()) << std::endl;
        std::exit(-3);
    }
    return 0;
}

/// `--png-dir`: every page of RANGE (all: none given) as "DIR/<name>-p001.png", one file per page, numbered as the app
/// numbers them (at least three digits, the page's number in the document).
int exportPngPages(const fs::path& infile, const fs::path& dir, const char* range, const char* layerRange, int dpi,
                   ExportBackgroundType exportBackground) {
    auto doc = loadDocumentOrExit(infile, exportBackground);
    const size_t count = doc->getPageCount();
    PageRangeVector pages;
    if (range) {
        pages = ElementRange::parse(range, count);
    } else if (count > 0) {
        pages.emplace_back(0, count - 1);
    }
    const int digits = std::max(3, static_cast<int>(std::to_string(std::max<size_t>(count, 1)).size()));
    const std::string stem = infile.stem().string();
    int written = 0;
    try {
        for (const auto& entry: pages) {
            for (size_t p = entry.first; p <= entry.last && p < count; ++p) {
                std::string number = std::to_string(p + 1);
                number.insert(0, static_cast<size_t>(std::max(0, digits - static_cast<int>(number.size()))), '0');
                const fs::path target = dir / (stem + "-p" + number + ".png");
                // (one page: upstream's export names the file as given)
                ExportHelper::exportImg(doc.get(), target, std::to_string(p + 1).c_str(), layerRange,
                                        dpi > 0 ? dpi : 300, -1, -1, exportBackground);
                std::cout << target.string() << std::endl;
                ++written;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << FS(_F("Error exporting image: {1}") % e.what()) << std::endl;
        std::exit(-3);
    }
    return written > 0 ? 0 : -3;
}

/// Upstream `--save`: create a .xopp with the given PDF as background.
int saveDoc(const fs::path& infile, const fs::path& outfile) {
    SaveHandler saver;
    auto handler = std::make_unique<DocumentHandler>();
    auto newDoc = std::make_unique<Document>(handler.get());
    if (!newDoc->readPdf(infile, /*initPages=*/true, false)) {
        std::cerr << FS(_F("Error reading PDF: {1}") % newDoc->getLastErrorMsg()) << std::endl;
        std::exit(-2);
    }
    const fs::path out = fs::absolute(outfile);
    saver.prepareSave(newDoc.get(), out);
    saver.saveTo(out);
    if (!saver.getErrorMessage().empty()) {
        std::cerr << FS(_F("Error saving document: {1}") % saver.getErrorMessage()) << std::endl;
        std::exit(-3);
    }
    return 0;
}

/// Fork: load a .xopp/.xoj and write it again (file format round trip).
int resaveDoc(const fs::path& infile, const fs::path& outfile) {
    auto doc = loadDocumentOrExit(infile, EXPORT_BACKGROUND_NONE);
    SaveHandler saver;
    const fs::path out = fs::absolute(outfile);
    // Like the GUI's "Save as": the document takes the new path first. SaveHandler derives the location of an
    // attached background PDF ("<name>.xopp.bg.pdf") from the document path.
    doc->setFilepath(out);
    saver.prepareSave(doc.get(), out);
    saver.saveTo(out);
    if (!saver.getErrorMessage().empty()) {
        std::cerr << FS(_F("Error saving document: {1}") % saver.getErrorMessage()) << std::endl;
        std::exit(-3);
    }
    return 0;
}

const char* elementTypeName(ElementType t) {
    switch (t) {
        case ELEMENT_STROKE:
            return "stroke";
        case ELEMENT_IMAGE:
            return "image";
        case ELEMENT_TEXIMAGE:
            return "teximage";
        case ELEMENT_TEXT:
            return "text";
        case ELEMENT_LINK:
            return "link";
    }
    return "unknown";
}

/// Fork: structural summary, used by the round-trip tests to compare documents.
int dumpDoc(const fs::path& infile) {
    auto doc = loadDocumentOrExit(infile, EXPORT_BACKGROUND_NONE);
    std::cout << "pages " << doc->getPageCount() << "\n";
    for (size_t p = 0; p < doc->getPageCount(); ++p) {
        auto page = doc->getPage(p);
        const PageType bg = page->getBackgroundType();
        std::cout << "page " << p << " size " << page->getWidth() << "x" << page->getHeight() << " bg "
                  << PageTypeHandler::getStringForPageTypeFormat(bg.format) << (bg.isPdfPage() ? " pdf#" : "")
                  << (bg.isPdfPage() ? std::to_string(page->getPdfPageNr()) : std::string()) << " layers "
                  << page->getLayerCount() << "\n";
        for (const Layer* layer: page->getLayersView()) {
            std::cout << "  layer '" << layer->getName() << "' visible " << layer->isVisible() << " elements "
                      << layer->getElementsView().size() << "\n";
            for (const Element* e: layer->getElementsView()) {
                std::cout << "    " << elementTypeName(e->getType()) << " color " << std::hex
                          << static_cast<uint32_t>(e->getColor()) << std::dec;
                if (e->getType() == ELEMENT_STROKE) {
                    const auto* s = static_cast<const Stroke*>(e);
                    std::cout << " tool " << static_cast<int>(s->getToolType()) << " width " << s->getWidth()
                              << " fill " << s->getFill() << " points " << s->getPointCount()
                              << " pressure " << s->hasPressure();
                }
                std::cout << "\n";
            }
        }
    }
    return 0;
}

/// Fork: time the full-page render of every page (baseline for the M6 MuPDF comparison).
int benchRender(const fs::path& infile, double zoom) {
    auto doc = loadDocumentOrExit(infile, EXPORT_BACKGROUND_ALL);
    using clock = std::chrono::steady_clock;
    double total = 0;
    for (size_t p = 0; p < doc->getPageCount(); ++p) {
        auto page = doc->getPage(p);
        const int w = static_cast<int>(page->getWidth() * zoom), h = static_cast<int>(page->getHeight() * zoom);
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t* cr = cairo_create(surf);
        cairo_scale(cr, zoom, zoom);
        const auto t0 = clock::now();
        xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL;
        DocumentView view;
        view.drawPage(page, cr, true, flags);
        cairo_surface_flush(surf);
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        total += ms;
        std::cout << "page " << p << " " << w << "x" << h << " " << ms << " ms\n";
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
    }
    std::cout << "total " << total << " ms, average "
              << (doc->getPageCount() ? total / static_cast<double>(doc->getPageCount()) : 0) << " ms/page\n";
    return 0;
}
}  // namespace

#ifdef XQT_CLI_HWR
namespace {
/// xournal-qt-cli hwr-lines DOC --out DIR [--text FILE] [--lang de] [--writer ID] [--licence ID]
int hwrLines(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    xqt::hwr::LineExport job;
    const QStringList args = QCoreApplication::arguments().mid(2);
    auto usage = [] {
        std::cerr << "usage: xournal-qt-cli hwr-lines <doc.xopp|pdf> --out <dir> [--text <transcripts.txt>] "
                     "[--lang en|de] [--writer <id>] [--licence <id>]\n"
                     "  The handwriting of the document as a line dataset (qt/research/hwr/train/FORMATS.md):\n"
                     "  dataset.json, lines.jsonl, images/ and strokes/ in <dir>. --text: one line of text per\n"
                     "  line of ink, in reading order. --licence: of the writer's own lines (default \"private\",\n"
                     "  marked noncommercial; any other licence is not marked).\n";
        return 1;
    };
    for (qsizetype i = 0; i < args.size(); ++i) {
        const QString& a = args[i];
        auto value = [&]() -> QString { return i + 1 < args.size() ? args[++i] : QString(); };
        if (a == QLatin1String("--out")) {
            job.out = value();
        } else if (a == QLatin1String("--text")) {
            job.texts = value();
        } else if (a == QLatin1String("--lang")) {
            job.language = value();
        } else if (a == QLatin1String("--writer")) {
            job.writer = value();
        } else if (a == QLatin1String("--licence") || a == QLatin1String("--license")) {
            job.licence = value();
            job.noncommercial = job.licence == QLatin1String("private");
        } else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            return usage();
        } else if (!a.startsWith(QLatin1String("--")) && job.document.isEmpty()) {
            job.document = a;
        } else {
            std::cerr << "unknown argument: " << a.toStdString() << "\n";
            return usage();
        }
    }
    if (job.document.isEmpty() || job.out.isEmpty() || job.language.isEmpty()) {
        return usage();
    }
    const xqt::hwr::LineExportResult r = xqt::hwr::exportLines(job);
    for (const QString& w: r.warnings) {
        std::cerr << "warning: " << w.toStdString() << "\n";
    }
    if (!r.ok) {
        std::cerr << r.error.toStdString() << "\n";
        return -3;
    }
    std::cout << r.lines << " lines written to " << job.out.toStdString();
    if (!job.texts.isEmpty()) {
        std::cout << " (" << std::min(r.lines, r.transcripts) << " with their text)";
    }
    std::cout << "\n";
    return 0;
}

/// xournal-qt-cli hwr-form FILLED --out DIR [--manifest FILE] [--writer ID] [--licence ID]
int hwrForm(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    xqt::hwr::FormExport job;
    const QStringList args = QCoreApplication::arguments().mid(2);
    auto usage = [] {
        std::cerr << "usage: xournal-qt-cli hwr-form <filled.xopp|annotated.pdf> --out <dir> [--manifest <file>] "
                     "[--writer <id>] [--licence <id>]\n"
                     "  A filled handwriting form (qt/research/hwr/forms/DESIGN.md) as a line dataset\n"
                     "  (qt/research/hwr/train/FORMATS.md): one line per box with strokes, with the box's text.\n"
                     "  The manifest is the one attached to the form's PDF, else --manifest. --licence: of the\n"
                     "  writer's own lines (default \"private\", marked noncommercial).\n";
        return 1;
    };
    for (qsizetype i = 0; i < args.size(); ++i) {
        const QString& a = args[i];
        auto value = [&]() -> QString { return i + 1 < args.size() ? args[++i] : QString(); };
        if (a == QLatin1String("--out")) {
            job.out = value();
        } else if (a == QLatin1String("--manifest")) {
            job.manifest = value();
        } else if (a == QLatin1String("--writer")) {
            job.writer = value();
        } else if (a == QLatin1String("--licence") || a == QLatin1String("--license")) {
            job.licence = value();
            job.noncommercial = job.licence == QLatin1String("private");
        } else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            return usage();
        } else if (!a.startsWith(QLatin1String("--")) && job.document.isEmpty()) {
            job.document = a;
        } else {
            std::cerr << "unknown argument: " << a.toStdString() << "\n";
            return usage();
        }
    }
    if (job.document.isEmpty() || job.out.isEmpty()) {
        return usage();
    }
    const xqt::hwr::FormExportResult r = xqt::hwr::exportForm(job);
    for (const QString& w: r.warnings) {
        std::cerr << "warning: " << w.toStdString() << "\n";
    }
    if (!r.ok) {
        std::cerr << r.error.toStdString() << "\n";
        return -3;
    }
    std::cout << r.lines << " lines of " << r.form.toStdString() << " written to " << job.out.toStdString() << " ("
              << r.math << " formulas; " << r.empty << " boxes empty, " << r.left << " drawings and marks left out, "
              << r.outside << " strokes in no box)\n";
    return 0;
}

/// xournal-qt-cli hwr-bench FILLED [--model DIR]... [--manifest FILE] [--out REPORT]
int hwrBench(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments().mid(2);
    auto usage = [] {
        std::cerr << "usage: xournal-qt-cli hwr-bench <filled.xopp|annotated.pdf> [--model <folder>]... "
                     "[--manifest <file>] [--out <report>]\n"
                     "  Runs the handwriting search on the whole pages of a filled form and compares with its\n"
                     "  manifest (qt/research/hwr/forms/DESIGN.md): lines and angles per box, CER, words found,\n"
                     "  search recall, text read in drawings, false hits; per section, kind, size and angle.\n"
                     "  --model: a model folder (model.json), several side by side (default: the built-in model).\n"
                     "  --out: writes <report>.json and <report>.md (else the Markdown to the output).\n"
                     "  ONNX Runtime: as the app finds it (XQT_ONNXRUNTIME=<path of the library> to choose).\n";
        return 1;
    };
    QString document, manifestFile, out;
    QStringList folders;
    for (qsizetype i = 0; i < args.size(); ++i) {
        const QString& a = args[i];
        auto value = [&]() -> QString { return i + 1 < args.size() ? args[++i] : QString(); };
        if (a == QLatin1String("--model")) {
            folders << value();
        } else if (a == QLatin1String("--manifest")) {
            manifestFile = value();
        } else if (a == QLatin1String("--out")) {
            out = value();
        } else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            return usage();
        } else if (!a.startsWith(QLatin1String("--")) && document.isEmpty()) {
            document = a;
        } else {
            std::cerr << "unknown argument: " << a.toStdString() << "\n";
            return usage();
        }
    }
    if (document.isEmpty()) {
        return usage();
    }
#ifndef XQT_HWR_ONNX
    std::cerr << "This build has no ONNX Runtime support (XQT_HWR_ONNX)\n";
    return -3;
#else
    auto loaded = xqt::DocumentSession::loadFile(fs::path(document.toStdString()));
    if (!loaded.document) {
        std::cerr << (loaded.error.empty() ? "Cannot open " + document.toStdString() : loaded.error) << "\n";
        return -2;
    }
    const xqt::hwr::FormManifest manifest =
            xqt::hwr::manifestOf(*loaded.document, fs::path(document.toStdString()), manifestFile);
    if (!manifest.valid()) {
        std::cerr << manifest.error.toStdString() << "\n";
        return -3;
    }
    if (folders.isEmpty()) {
        // The built-in model that reads the form's language (else the first one)
        const auto bundled = xqt::hwr::HandwritingSearch::bundledModels(
                xqt::hwr::HandwritingSearch::bundledModelsDir(xqt::AppContext::defaultResourceDir()));
        const QString lang = manifest.language.isEmpty() ? QStringLiteral("en") : manifest.language;
        auto it = std::find_if(bundled.begin(), bundled.end(),
                               [&](const xqt::hwr::ModelInfo& m) { return m.reads(lang); });
        if (it == bundled.end() && !bundled.empty()) {
            it = bundled.begin();
        }
        if (it == bundled.end()) {
            std::cerr << "No built-in handwriting model found (give one with --model)\n";
            return -3;
        }
        folders << it->folder;
    }
    std::vector<xqt::hwr::BenchModel> models;
    for (const QString& folder: folders) {
        const xqt::hwr::ModelInfo info = xqt::hwr::ModelInfo::read(folder);
        if (!info.valid()) {
            std::cerr << folder.toStdString() << ": " << info.error.toStdString() << "\n";
            return -3;
        }
        xqt::hwr::BenchModel m;
        m.name = info.name;
        for (int n = 2; std::any_of(models.begin(), models.end(),
                                    [&](const xqt::hwr::BenchModel& o) { return o.name == m.name; });
             ++n) {
            m.name = info.name + QStringLiteral("-%1").arg(n);
        }
        m.folder = folder;
        m.recognizer = xqt::hwr::onnxRecognizerFor(folder);
        QString why;
        if (!m.recognizer->ready(&why)) {
            std::cerr << folder.toStdString() << ": " << why.toStdString() << "\n";
            return -3;
        }
        models.push_back(std::move(m));
    }
    const xqt::hwr::BenchReport report =
            xqt::hwr::runBench(*loaded.document, manifest, models, QFileInfo(document).fileName());
    if (!report.ok) {
        std::cerr << report.error.toStdString() << "\n";
        return -3;
    }
    if (out.isEmpty()) {
        std::cout << report.markdown.toStdString();
        return 0;
    }
    QString base = out;
    if (base.endsWith(QLatin1String(".json")) || base.endsWith(QLatin1String(".md"))) {
        base = base.left(base.lastIndexOf(u'.'));
    }
    if (const QString dir = QFileInfo(base).absolutePath(); !QDir().mkpath(dir)) {
        std::cerr << "Cannot create " << dir.toStdString() << "\n";
        return -3;
    }
    for (const auto& [path, data]: {std::pair{base + QStringLiteral(".json"), QJsonDocument(report.json).toJson()},
                                    std::pair{base + QStringLiteral(".md"), report.markdown.toUtf8()}}) {
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
            std::cerr << "Cannot write " << path.toStdString() << "\n";
            return -3;
        }
        std::cout << path.toStdString() << "\n";
    }
    return 0;
#endif
}
}  // namespace
#endif

#ifdef XQT_CLI_SESSION
namespace {
/// xournal-qt-cli export-xopp PDF [--version N] [-o OUT.xopp]
int exportVersion(int argc, char* argv[]) {
    auto usage = [] {
        std::cerr << "usage: xournal-qt-cli export-xopp <file.pdf> [--version N] [-o out.xopp]\n"
                     "  The Xournal++ document a PDF with notes carries: of its latest version, or of version N of\n"
                     "  its version history (an older version's is rebuilt from its delta and checked). Its PDF\n"
                     "  background is the PDF by its name. Default output: <name>.xopp, or <name>.v<N>.xopp.\n";
        return 1;
    };
    fs::path pdf, out;
    int version = -1;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--version" || a == "-v") && i + 1 < argc) {
            try {
                version = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                return usage();
            }
        } else if ((a == "-o" || a == "--out") && i + 1 < argc) {
            out = fs::path(argv[++i]);
        } else if (a == "-h" || a == "--help") {
            return usage();
        } else if (pdf.empty() && a.rfind("-", 0) != 0) {
            pdf = fs::path(a);
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            return usage();
        }
    }
    if (pdf.empty()) {
        return usage();
    }
    if (out.empty()) {
        out = pdf.stem();
        out += version >= 0 ? ".v" + std::to_string(version) + ".xopp" : std::string(".xopp");
    }
    std::string error;
    if (!xqt::PdfHistory::exportXopp(pdf, version, out, error)) {
        std::cerr << error << "\n";
        return -3;
    }
    std::cout << out.string() << "\n";
    return 0;
}
}  // namespace
#endif

int main(int argc, char* argv[]) {
    // Same as upstream initCAndCoutLocales(): numbers in C locale for cairo/PDF output.
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");
#ifdef _WIN32
    // std::filesystem converts narrow strings with the C library's character set: make that UTF-8, as everywhere
    // else in the program (see qt/docs/development/windows.md). XQT_NO_UTF8_LOCALE=1 skips it (a diagnostic).
    if (!g_getenv("XQT_NO_UTF8_LOCALE")) {
        setlocale(LC_CTYPE, ".UTF-8");
    }
    // Text with Pango's fontconfig backend: its Windows one dies drawing into images (PNG export).
    xqt::windows::useFontconfig();
#endif
    std::cout.imbue(std::locale());
#ifdef XQT_CLI_HWR
    if (argc >= 2 && std::string(argv[1]) == "hwr-lines") {
        return hwrLines(argc, argv);
    }
    if (argc >= 2 && std::string(argv[1]) == "hwr-form") {
        return hwrForm(argc, argv);
    }
    if (argc >= 2 && std::string(argv[1]) == "hwr-bench") {
        return hwrBench(argc, argv);
    }
#endif
#ifdef XQT_CLI_SESSION
    if (argc >= 2 && std::string(argv[1]) == "export-xopp") {
        return exportVersion(argc, argv);
    }
    xqt::paper::install();  // textured paper in exports, as the app draws it (qt/docs/features/dark-pages.md)
#endif

    gchar** optFilename = nullptr;
    gchar* pdfDir = nullptr;
    gchar* pngDir = nullptr;
    gchar* pdfFilename = nullptr;
    gchar* imgFilename = nullptr;
    gchar* docFilename = nullptr;
    gchar* resaveFilename = nullptr;
    gboolean exportNoBackground = false;
    gboolean exportNoRuling = false;
    gboolean progressiveMode = false;
    gboolean showVersion = false;
    gboolean dump = false;
    gchar* exportRange = nullptr;
    gchar* exportLayerRange = nullptr;
    gchar* exportPdfBackend = nullptr;
    gint exportPngDpi = -1;
    gint exportPngWidth = -1;
    gint exportPngHeight = -1;
    gdouble benchZoom = 0;

    std::array options = {
            GOptionEntry{G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &optFilename, "", "FILE"},
            GOptionEntry{"version", 0, 0, G_OPTION_ARG_NONE, &showVersion, "Get version of xournal-qt-cli", nullptr},
            GOptionEntry{"create-pdf", 'p', 0, G_OPTION_ARG_FILENAME, &pdfFilename, "Export FILE as PDF", "PDFFILE"},
            GOptionEntry{"create-img", 'i', 0, G_OPTION_ARG_FILENAME, &imgFilename,
                         "Export FILE as image files (one per page)", "IMGFILE"},
            GOptionEntry{"save", 's', 0, G_OPTION_ARG_FILENAME, &docFilename,
                         "Save xopp-file with the background PDF specified as FILE", "XOPPFILE"},
            GOptionEntry{"export-no-background", 0, 0, G_OPTION_ARG_NONE, &exportNoBackground,
                         "Export without background", nullptr},
            GOptionEntry{"export-no-ruling", 0, 0, G_OPTION_ARG_NONE, &exportNoRuling, "Export without ruling",
                         nullptr},
            GOptionEntry{"export-layers-progressively", 0, 0, G_OPTION_ARG_NONE, &progressiveMode,
                         "Export layers progressively (PDF)", nullptr},
            GOptionEntry{"export-range", 0, 0, G_OPTION_ARG_STRING, &exportRange, "Only export the pages in RANGE",
                         "RANGE"},
            GOptionEntry{"export-layer-range", 0, 0, G_OPTION_ARG_STRING, &exportLayerRange,
                         "Only export the layers in RANGE", "RANGE"},
            GOptionEntry{"export-png-dpi", 0, 0, G_OPTION_ARG_INT, &exportPngDpi, "Set DPI for PNG exports", "N"},
            GOptionEntry{"export-png-width", 0, 0, G_OPTION_ARG_INT, &exportPngWidth, "Set page width for PNG exports",
                         "N"},
            GOptionEntry{"export-png-height", 0, 0, G_OPTION_ARG_INT, &exportPngHeight,
                         "Set page height for PNG exports", "N"},
            GOptionEntry{"export-pdf-backend", 0, 0, G_OPTION_ARG_STRING, &exportPdfBackend,
                         "Use the given backend for PDF export (default, cairo, qpdf)", "BACKEND"},
            GOptionEntry{"resave", 0, 0, G_OPTION_ARG_FILENAME, &resaveFilename,
                         "[xournal-qt] Load FILE and save it again as XOPPFILE", "XOPPFILE"},
            GOptionEntry{"dump", 0, 0, G_OPTION_ARG_NONE, &dump, "[xournal-qt] Print a structural summary of FILE",
                         nullptr},
            GOptionEntry{"pdf-dir", 0, 0, G_OPTION_ARG_FILENAME, &pdfDir,
                         "[xournal-qt] Export every FILE as PDF into DIR (batch)", "DIR"},
            GOptionEntry{"png-dir", 0, 0, G_OPTION_ARG_FILENAME, &pngDir,
                         "[xournal-qt] Export the pages of every FILE as DIR/<name>-p001.png, ... (default 300 dpi)",
                         "DIR"},
            GOptionEntry{"bench-render", 0, 0, G_OPTION_ARG_DOUBLE, &benchZoom,
                         "[xournal-qt] Time rendering every page of FILE at ZOOM", "ZOOM"},
            GOptionEntry{nullptr}};

    GOptionContext* context = g_option_context_new("FILE - headless Xournal++ core tool (xournal-qt)");
    g_option_context_add_main_entries(context, options.data(), nullptr);
    GError* error = nullptr;
#ifdef _WIN32
    // The arguments in UTF-8 (argv is in the ANSI code page), as GLib expects file names on Windows.
    (void)argc;
    (void)argv;
    gchar** args = g_win32_get_command_line();
    const bool parsed = g_option_context_parse_strv(context, &args, &error);
    g_strfreev(args);
#else
    const bool parsed = g_option_context_parse(context, &argc, &argv, &error);
#endif
    if (!parsed) {
        std::cerr << error->message << std::endl;
        g_error_free(error);
        g_option_context_free(context);
        return 1;
    }
    g_option_context_free(context);

    if (showVersion) {
        std::cout << xoj::util::getVersionInfo() << std::endl;
        return 0;
    }
    if (!optFilename || !*optFilename) {
        std::cerr << "No input file given (see --help)" << std::endl;
        return 1;
    }
    const fs::path input = Util::fromGFilename(*optFilename);
    // Many documents in one go: DIR/<name>.pdf for each of them (the exit code counts the ones that failed)
    if (pdfDir) {
        const fs::path directory = Util::fromGFilename(pdfDir);
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (!fs::is_directory(directory, ec)) {
            std::cerr << "Not a directory: " << directory.string() << std::endl;
            return 1;
        }
        const ExportBackgroundType batchBg = exportNoBackground ? EXPORT_BACKGROUND_NONE :
                                             exportNoRuling     ? EXPORT_BACKGROUND_UNRULED :
                                                                  EXPORT_BACKGROUND_ALL;
        int failed = 0;
        int done = 0;
        for (gchar** file = optFilename; *file; ++file) {
            const fs::path one = Util::fromGFilename(*file);
            fs::path target = directory / one.filename();
            target.replace_extension(".pdf");
            try {
                if (exportPdf(one, target, exportRange, exportLayerRange, batchBg, progressiveMode,
                              ExportBackend::fromString(exportPdfBackend)) != 0) {
                    ++failed;
                    continue;
                }
                ++done;
                std::cout << one.filename().string() << " -> " << target.string() << std::endl;
            } catch (const std::exception& e) {
                std::cerr << one.string() << ": " << e.what() << std::endl;
                ++failed;
            }
        }
        std::cout << done << " exported, " << failed << " failed" << std::endl;
        return failed == 0 ? 0 : -3;
    }
    // The pages of every document as pictures, named as the app names them
    if (pngDir) {
        const fs::path directory = Util::fromGFilename(pngDir);
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (!fs::is_directory(directory, ec)) {
            std::cerr << "Not a directory: " << directory.string() << std::endl;
            return 1;
        }
        const ExportBackgroundType pngBg = exportNoBackground ? EXPORT_BACKGROUND_NONE :
                                           exportNoRuling     ? EXPORT_BACKGROUND_UNRULED :
                                                                EXPORT_BACKGROUND_ALL;
        int failed = 0;
        for (gchar** file = optFilename; *file; ++file) {
            failed += exportPngPages(Util::fromGFilename(*file), directory, exportRange, exportLayerRange,
                                     exportPngDpi, pngBg) != 0;
        }
        return failed == 0 ? 0 : -3;
    }
    const ExportBackgroundType bg = exportNoBackground ? EXPORT_BACKGROUND_NONE :
                                    exportNoRuling     ? EXPORT_BACKGROUND_UNRULED :
                                                         EXPORT_BACKGROUND_ALL;
    try {
        if (pdfFilename) {
            return exportPdf(input, Util::fromGFilename(pdfFilename), exportRange, exportLayerRange, bg,
                             progressiveMode, ExportBackend::fromString(exportPdfBackend));
        }
        if (imgFilename) {
            return exportImg(input, Util::fromGFilename(imgFilename), exportRange, exportLayerRange, exportPngDpi,
                             exportPngWidth, exportPngHeight, bg);
        }
        if (docFilename) {
            return saveDoc(input, Util::fromGFilename(docFilename));
        }
        if (resaveFilename) {
            return resaveDoc(input, Util::fromGFilename(resaveFilename));
        }
        if (dump) {
            return dumpDoc(input);
        }
        if (benchZoom > 0) {
            return benchRender(input, benchZoom);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cout << xoj::util::getVersionInfo() << std::endl;
        return 1;
    }
    std::cerr << "Nothing to do: use --create-img, --create-pdf, --save, --resave, --dump or --bench-render"
              << std::endl;
    return 1;
}
