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
 *
 * @license GNU GPLv2 or later
 */
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
#include "util/PathUtil.h"
#include "util/PlaceholderString.h"
#include "util/VersionInfo.h"
#include "util/i18n.h"
#include "util/raii/CStringWrapper.h"
#include "view/DocumentView.h"

#include "filesystem.h"

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

int main(int argc, char* argv[]) {
    // Same as upstream initCAndCoutLocales(): numbers in C locale for cairo/PDF output.
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");
    std::cout.imbue(std::locale());

    gchar** optFilename = nullptr;
    gchar* pdfDir = nullptr;
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
            GOptionEntry{"bench-render", 0, 0, G_OPTION_ARG_DOUBLE, &benchZoom,
                         "[xournal-qt] Time rendering every page of FILE at ZOOM", "ZOOM"},
            GOptionEntry{nullptr}};

    GOptionContext* context = g_option_context_new("FILE - headless Xournal++ core tool (xournal-qt)");
    g_option_context_add_main_entries(context, options.data(), nullptr);
    GError* error = nullptr;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
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
