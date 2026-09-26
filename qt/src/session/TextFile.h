/*
 * xournal-qt: a text file (.md, .txt, or another text file edited as plain text) opened for editing.
 *
 * The editor works on the text as the Markdown engine sees it: UTF-8 without a byte order mark, lines ending in "\n".
 * The file may differ (a byte order mark, "\r\n" line ends, a mix of both kinds): what it was is kept here, and
 * saving gives the file back byte for byte where the text was not changed:
 *  - the bytes before the first change and after the last one are the file's own bytes (a byte order mark, "\r\n",
 *    a missing newline at the end: as they were);
 *  - the changed part in between is written with the line ends the file mostly has ("\r\n" in a Windows file).
 * A file with both kinds of line ends and changes far apart can therefore get the main kind in the lines between
 * the changes.
 *
 * Writing is atomic (QSaveFile: another name first, then renamed over the file), and the file's size, time and bytes
 * are remembered, so a change by another program is told from our own save (Stamp, changedOnDisk).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <string>

#include "filesystem.h"

namespace xqt {

class TextFile {
public:
    enum class Kind {
        Markdown,  ///< .md: rendered as Markdown while it is edited
        Plain,     ///< .txt, or another text file edited as plain text: no rendering
    };
    /// Where the text of a page begins on its page (TextFlow::MARGIN, the page's Markdown text; a text file's pages
    /// are A4, PageMargins.h).
    static constexpr double PAGE_MARGIN = 56.7;
    /// Bigger files open read-only (their start: MarkdownFile::MAX_BYTES).
    static constexpr size_t MAX_EDIT_BYTES = 2 * 1024 * 1024;

    /// What the file on disk was when it was read or written last.
    struct Stamp {
        std::uintmax_t size = 0;
        std::int64_t time = 0;  ///< modification time (ns)
        bool operator==(const Stamp& o) const { return size == o.size && time == o.time; }
        bool operator!=(const Stamp& o) const { return !(*this == o); }
    };
    static Stamp stampOf(const fs::path& file);

    TextFile() = default;
    /// Read a file. False if it cannot be read (`error`). Whether it can be edited: editable().
    bool load(const fs::path& file, Kind kind, std::string& error);
    /// Take these bytes as the file's (e.g. read again after a change on disk).
    void setBytes(std::string bytes);

    const fs::path& path() const { return file; }
    void setPath(const fs::path& p) { file = p; }
    Kind kind() const { return textKind; }
    void setKind(Kind k) { textKind = k; }

    /// The text for the editor (UTF-8, "\n" line ends, no byte order mark).
    const std::string& text() const { return saved; }
    /// The file as it was read or written last.
    const std::string& bytes() const { return raw; }
    const Stamp& stamp() const { return diskStamp; }
    void setStamp(const Stamp& s) { diskStamp = s; }

    /// It can be edited: valid UTF-8, not longer than MAX_EDIT_BYTES. (Write access is not checked here.)
    bool editable() const { return utf8 && !tooBig; }
    bool isUtf8() const { return utf8; }
    bool isTooBig() const { return tooBig; }
    bool hasBom() const { return bom; }
    /// Most of its line ends are "\r\n".
    bool crlf() const { return mainCrlf; }

    /// The bytes to write for `text` (see the top of this file).
    std::string encode(const std::string& text) const;
    /// Write `text` to `target` atomically (the file's own path if empty). On success the file is `text` from now on
    /// (bytes, stamp; a new target becomes the path). False if that failed (`error`).
    bool save(const std::string& text, std::string& error, const fs::path& target = {});
    /// Only write (any thread): the bytes to `target`, atomically. False if that failed (`error`).
    static bool writeAtomically(const fs::path& target, const std::string& bytes, std::string& error);
    /// The file was written (from another thread, by writeAtomically): it holds `text` as `bytes` now.
    void written(const fs::path& target, const std::string& text, std::string bytes);

    /// The file on disk is not what was read or written last (size and time differ, and so do the bytes). `now`: what
    /// it holds now (read when the stamp differs).
    bool changedOnDisk(std::string* now = nullptr);

    /// The text of these bytes: without a byte order mark, "\r\n" as "\n".
    static std::string normalized(const std::string& bytes);
    static bool validUtf8(const std::string& s);

private:
    void analyse();

    fs::path file;
    Kind textKind = Kind::Markdown;
    std::string raw;    ///< the file's bytes
    std::string saved;  ///< its text (normalized)
    Stamp diskStamp;
    bool bom = false;
    bool mainCrlf = false;
    bool utf8 = true;
    bool tooBig = false;
};

}  // namespace xqt
