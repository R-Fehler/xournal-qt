/*
 * xournal-qt: where the objects a PDF write makes go: a QPDF that is written in full (FullSink), or an incremental
 * update of a file (UpdateSink, IncrementalPdf::Update, which also needs to know which objects of the file change).
 * Code that writes through an ObjectSink writes both kinds of file the same way: the marker, the text layer of the
 * handwriting, the embedded files (HybridMarker.cpp), the version history (HybridHistory.cpp) and the outline's
 * bookmarks (PdfBookmarks).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include "IncrementalPdf.h"
#include "filesystem.h"

namespace xqt {

class ObjectSink {
public:
    virtual ~ObjectSink() = default;
    /// A new indirect object with this (direct) value.
    virtual QPDFObjectHandle add(QPDFObjectHandle value) = 0;
    /// A new stream: its dictionary (direct, complete: it may not be changed afterwards) and data.
    virtual QPDFObjectHandle addStream(QPDFObjectHandle dict, const std::string& data) = 0;
    /// A new stream of a file's bytes (read when the PDF is written where it can be, else now).
    virtual QPDFObjectHandle addFileStream(QPDFObjectHandle dict, const fs::path& file) = 0;
    /// This object of the file is about to be changed.
    virtual void touch(QPDFObjectHandle object) = 0;
};

/// A PDF written in full: new objects of `q`; nothing to touch.
class FullSink final: public ObjectSink {
public:
    explicit FullSink(QPDF& q): q(q) {}
    QPDFObjectHandle add(QPDFObjectHandle value) override;
    QPDFObjectHandle addStream(QPDFObjectHandle dict, const std::string& data) override;
    QPDFObjectHandle addFileStream(QPDFObjectHandle dict, const fs::path& file) override;
    void touch(QPDFObjectHandle) override {}

private:
    QPDF& q;
};

/// An incremental update (IncrementalPdf::Update): new objects numbered by it, changed ones touched.
class UpdateSink final: public ObjectSink {
public:
    explicit UpdateSink(IncrementalPdf::Update& u): u(u) {}
    QPDFObjectHandle add(QPDFObjectHandle value) override { return u.add(value); }
    QPDFObjectHandle addStream(QPDFObjectHandle dict, const std::string& data) override {
        return u.addStream(dict, data);
    }
    QPDFObjectHandle addFileStream(QPDFObjectHandle dict, const fs::path& file) override;
    void touch(QPDFObjectHandle object) override { u.touch(object); }

private:
    IncrementalPdf::Update& u;
};

}  // namespace xqt
