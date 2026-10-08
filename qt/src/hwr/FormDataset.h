/*
 * xournal-qt: a filled handwriting form as a line dataset for training (qt/research/hwr/forms/DESIGN.md, "From a
 * filled form to data and numbers"; qt/research/hwr/train/FORMATS.md §1, "kind": "ink"). `xournal-qt-cli hwr-form`
 * runs it.
 *
 * The form's manifest (FormManifest.h) says where its boxes are and what was to be written into each. Every stroke goes
 * to the box that holds most of its points (FormManifest.h: assign()); the layout of the search plays no part, so a
 * box's text is its strokes' text whatever the search would make of them. Each box with strokes becomes one line of
 * the dataset:
 *  - its strokes turned upright (by -angle around (0, 0), as InkLayout turns a line at an angle), relative to their
 *    top-left: strokes/<id>.json, as LineDataset writes it;
 *  - images/<id>.png: those strokes drawn exactly as LineImage draws a line for the recognisers;
 *  - a line of lines.jsonl: { "id", "image", "text", "lang", "writer", "strokes", and "angle", "kind", "box_id",
 *    "form", "page" } ("math": true for formulas, which training may skip).
 * Boxes of kind drawing, mark and free are left out, boxes left empty are skipped; both are counted. Ids are
 * "<writer>-<form>-<box id>" (letters, digits and "_"). dataset.json as LineDataset writes it ("source": the file).
 *
 * Any thread (no Qt GUI objects but QImage).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QString>
#include <QStringList>

class Document;

namespace xqt::hwr {

struct FormManifest;

struct FormExport {
    QString document;  ///< the filled form: a .xopp on the form's PDF, or a PDF with notes
    QString manifest;  ///< a manifest file ("": the one attached to the form's PDF)
    QString out;       ///< the dataset's folder (made if missing)
    QString writer = QStringLiteral("me");
    QString licence = QStringLiteral("private");
    bool noncommercial = true;
};

struct FormExportResult {
    bool ok = false;
    int lines = 0;    ///< boxes written
    int math = 0;     ///< of them formulas (flagged)
    int empty = 0;    ///< boxes for text without strokes (skipped)
    int left = 0;     ///< boxes of kind drawing, mark or free (left out)
    int outside = 0;  ///< strokes in no box
    QString form;     ///< the manifest's form ("xqt-hwr-en")
    QString error;
    QStringList warnings;
};

FormExportResult exportForm(const FormExport& job);
/// The same for a document in memory with its manifest (`job.document` only names it).
FormExportResult exportForm(const FormExport& job, Document& document, const FormManifest& manifest);

}  // namespace xqt::hwr
