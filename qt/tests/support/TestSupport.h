/*
 * xournal-qt: helpers shared by the test binaries (qt/docs/testing/README.md, "Shared helpers"): waiting for a state,
 * files, the PDFs the tests make, upstream's fixture files.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <filesystem>
#include <functional>
#include <source_location>
#include <string>
#include <vector>

#include <QString>

namespace xqt::test {

namespace fs = std::filesystem;

/// Runs the event loop until `done` is true, for at most `ms`. A timeout FAILS the test (non-fatally, reported at the
/// caller's line); the result says whether it became true, so `ASSERT_TRUE(waitFor(…))` stops the test there.
/// Returns as soon as it is true: the time is for a machine slowed down by other work.
bool waitFor(const std::function<bool()>& done, int ms = 5000,
             std::source_location where = std::source_location::current());
/// The same without failing, for a state that may or may not come (the caller looks at what happened afterwards).
bool waitUpTo(const std::function<bool()>& done, int ms);
/// Runs the event loop for `ms`: a fixed wait, only where no state says that something is done (e.g. to show that
/// something does NOT happen within that time).
void processEventsFor(int ms);

/// The whole file as bytes ("" when it is missing).
std::string readFile(const fs::path& p);
/// Writes `bytes` as the whole file; makes the folders it is in.
void writeFile(const fs::path& p, const std::string& bytes);
/// gzip-compressed bytes uncompressed ("" when they are not gzip).
std::string gunzip(const std::string& data);
/// A gzip file (a .xopp) uncompressed; a file that is not compressed comes back as it is.
std::string gunzipFile(const fs::path& p);

/// How makeTextPdf writes its pages: the size of the page and where the text starts (points), the font size.
struct TextPdfStyle {
    double fontSize = 24;
    double x = 72;
    double y = 100;
    double width = 595;
    double height = 842;
};
/// A PDF with one page per string, each with that text (one line, cairo's "Sans"); makes the folders it is in.
void makeTextPdf(const fs::path& p, const std::vector<std::string>& pageTexts, const TextPdfStyle& style = {});
/// "<prefix>1" … "<prefix>n": the texts of n numbered pages for makeTextPdf.
std::vector<std::string> numbered(const std::string& prefix, int n);

/// A file of upstream's fixtures (test/files). They are read only: copy one into a temporary folder to change it.
fs::path fixture(const std::u8string& relative);
/// The same as a QString (for AppController's API).
QString fixturePath(const std::u8string& relative);
/// A path as a QString (UTF-8 on every platform).
QString qstr(const fs::path& p);

}  // namespace xqt::test
