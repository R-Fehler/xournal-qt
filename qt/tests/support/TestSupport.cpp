/*
 * xournal-qt: helpers shared by all test binaries.
 *
 * @license GNU GPLv2 or later
 */
#include "TestSupport.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QTimer>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <zlib.h>

namespace xqt::test {

namespace {
/// Runs the event loop until `done` (when given) or the deadline. It sleeps between events instead of spinning: a busy
/// loop took a whole core from the render threads of the tests that run beside it (ctest -j). A timer wakes it every
/// few ms to look again, for states that other threads change without an event.
bool run(const std::function<bool()>& done, int ms) {
    if (done && done()) {
        return true;
    }
    QDeadlineTimer deadline(ms);
    QTimer tick;
    tick.setInterval(5);
    tick.start();
    QTimer end;
    end.setSingleShot(true);
    end.start(std::max(ms, 0));
    for (;;) {
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents);
        if (done && done()) {
            return true;
        }
        if (deadline.hasExpired()) {
            return done && done();
        }
    }
}
}  // namespace

bool waitFor(const std::function<bool()>& done, int ms, std::source_location where) {
    if (run(done, ms)) {
        return true;
    }
    ADD_FAILURE_AT(where.file_name(), static_cast<int>(where.line()))
            << "waited " << ms << " ms for a state that did not come";
    return false;
}

bool waitUpTo(const std::function<bool()>& done, int ms) { return run(done, ms); }

void processEventsFor(int ms) { run(nullptr, ms); }

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const fs::path& p, const std::string& bytes) {
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }
    std::ofstream(p, std::ios::binary) << bytes;
}

std::string gunzip(const std::string& data) {
    if (data.size() < 2 || static_cast<unsigned char>(data[0]) != 0x1f || static_cast<unsigned char>(data[1]) != 0x8b) {
        return {};
    }
    z_stream z{};
    if (inflateInit2(&z, 15 + 32) != Z_OK) {
        return {};
    }
    std::string out;
    char buf[65536];
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    z.avail_in = static_cast<uInt>(data.size());
    while (true) {
        z.next_out = reinterpret_cast<Bytef*>(buf);
        z.avail_out = sizeof buf;
        const int rc = inflate(&z, Z_NO_FLUSH);
        out.append(buf, sizeof buf - z.avail_out);
        if (rc != Z_OK) {
            break;
        }
    }
    inflateEnd(&z);
    return out;
}

std::string gunzipFile(const fs::path& p) {
    gzFile in = gzopen(p.string().c_str(), "rb");
    std::string out;
    char buf[65536];
    for (int n; in && (n = gzread(in, buf, sizeof buf)) > 0;) {
        out.append(buf, static_cast<size_t>(n));
    }
    if (in) {
        gzclose(in);
    }
    return out;
}

void makeTextPdf(const fs::path& p, const std::vector<std::string>& pageTexts, const TextPdfStyle& style) {
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), style.width, style.height);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, style.fontSize);
    for (const auto& text: pageTexts) {
        cairo_move_to(cr, style.x, style.y);
        cairo_show_text(cr, text.c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

std::vector<std::string> numbered(const std::string& prefix, int n) {
    std::vector<std::string> texts;
    for (int i = 1; i <= n; ++i) {
        texts.push_back(prefix + std::to_string(i));
    }
    return texts;
}

fs::path fixture(const std::u8string& relative) {
    return fs::path(std::u8string(u8"" XQT_TEST_FILES_DIR) + u8"/" + relative);
}

QString fixturePath(const std::u8string& relative) {
    const std::u8string p = fixture(relative).u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(p.data()), static_cast<qsizetype>(p.size()));
}

QString qstr(const fs::path& p) {
    const std::u8string s = p.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(s.data()), static_cast<qsizetype>(s.size()));
}

}  // namespace xqt::test
