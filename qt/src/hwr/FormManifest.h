/*
 * xournal-qt: a handwriting form's manifest (qt/research/hwr/forms/DESIGN.md): the boxes of a copy-out document, each
 * with the text the writer is asked to write into it, and which pen strokes of a filled form belong to which box.
 *
 * The manifest is "<form>.manifest.json", embedded in the form's PDF as an attached file (read with qpdf: any attached
 * file whose name ends in ".manifest.json"); a filled form is a .xopp with that PDF as its background, or a PDF with
 * notes. Boxes are in mm from the page's top-left (`box_mm`: x, y, width, height upright) and turned by `angle`
 * (degrees, clockwise on the page, as ink::Word::angle) around their centre; here they are in page points (72 / 25.4
 * per mm). A form page is the PDF page of the document's page (1-based), else the document page's number.
 *
 * A stroke belongs to a box that holds more than half of its points, tested in the box's own turned frame; of several
 * (a label inside its drawing's box, `in`) the smallest (assign()). `xournal-qt-cli hwr-form` (FormDataset.h) and `hwr-bench`
 * (FormBench.h) both map strokes so.
 *
 * Any thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include "InkLayout.h"
#include "filesystem.h"

class Document;
class XojPage;

namespace xqt::hwr {

constexpr double PT_PER_MM = 72.0 / 25.4;

struct FormItem {
    QString id;       ///< "3.12"
    int page = 1;     ///< of the form, 1-based
    QString section;  ///< "A".."H"
    QString kind;     ///< line, word, chars, number, label, math, drawing, mark, free
    QString text;     ///< what the writer copies (NFC)
    QString lang;     ///< the item's language ("": the form's)
    QString latex;    ///< math: the typeset formula
    QRectF boxMm;     ///< upright, mm from the page's top-left
    double angle = 0;         ///< the writing direction, degrees clockwise (0 left to right, 90 downwards)
    double xHeightMm = 0;     ///< the letter height asked for (0: not given)
    bool guides = false;
    QStringList tags;
    QStringList search;  ///< the words a search should find in the box (the manifest's, else searchWords())
    QString context;
    QString in;  ///< the drawing or mark box this box lies in (a label of a flow chart): its words are not the drawing's

    /// Text is expected (not drawing, mark or free).
    bool textual() const;
    /// Part of the training data: textual (math too, flagged).
    bool forTraining() const { return textual(); }
    /// The box upright in page points.
    QRectF box() const;
    QPointF centre() const { return box().center(); }
    /// The point (page points) is inside the box, in the box's turned frame.
    bool contains(QPointF p) const;
    /// A point of the page in the box's frame (turned by -angle around the box's centre).
    QPointF framed(QPointF p) const;
};

struct FormManifest {
    QString form;  ///< "xqt-hwr-en"
    int version = 0;
    QString language;  ///< "en"
    QSizeF pageSizeMm;
    int pages = 0;
    std::vector<FormItem> items;
    QString error;  ///< why it is not a manifest ("": it is one)

    bool valid() const { return error.isEmpty(); }
    /// The language of an item (its own, else the form's, else "en").
    QString languageOf(const FormItem& item) const;
    /// The items of a form page.
    std::vector<const FormItem*> itemsOn(int page) const;
    static FormManifest parse(const QByteArray& json);
};

/// The words of a text a search looks for by default: its words (letters and digits, as the search splits text) of 3
/// or more letters, each once, as written.
QStringList searchWords(const QString& text);

/// The manifest attached to a PDF ("*.manifest.json"; empty if there is none, the file's name in `name`).
QByteArray embeddedManifest(const fs::path& pdf, QString* name = nullptr);

/// A filled form's manifest: `manifestFile` if given, else the one attached to the document's background PDF, else to
/// `file` itself (a PDF with notes). Error set if none is found.
FormManifest manifestOf(const Document& document, const fs::path& file, const QString& manifestFile = {});

/// The form page of a document's page (`index` 0-based): its PDF page + 1, else index + 1.
int formPageOf(const XojPage& page, size_t index);

/// The box of each stroke: an index into `items` (the smallest holding more than half of its points), or -1.
std::vector<int> assign(const std::vector<InkStroke>& strokes, const std::vector<const FormItem*>& items);

}  // namespace xqt::hwr
