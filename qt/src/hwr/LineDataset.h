/*
 * xournal-qt: a document's handwriting as a line dataset for training and evaluating models
 * (qt/research/hwr/train/FORMATS.md, §1, "kind": "ink"; qt/docs/features/handwriting-search.md, "Your handwriting as a
 * dataset"). `xournal-qt-cli hwr-lines` runs it.
 *
 * Every page's ink is laid out in lines and words as the search does (InkLayout.h); each line, in reading order (pages
 * in order, lines top to bottom), becomes
 *  - images/<id>.png: the line drawn exactly as the app draws it for its recognisers (LineImage.h: 128 px high,
 *    black ink with round caps on white, a quarter of the height as margin), grayscale;
 *  - strokes/<id>.json: its ink, { "width", "height", "strokes": [ { "points": [[x, y, pressure], ...], "width" } ] }
 *    in points relative to the line's top-left (pressure: the point's width over the stroke's, 1 without pressure);
 *  - a line of lines.jsonl: { "id", "image", "text", "lang", "writer", "strokes", "page", "line" }.
 * The texts come from a transcript file, one line of text per line of ink in the same reading order (empty lines and
 * lines starting with "#" are skipped); NFC-normalised. Without one, "text" is "" (a set to read, not to train on).
 * The ids are "<writer>-<document>-p<page>-l<line>". dataset.json names the set: { "name", "version": 1,
 * "languages", "licence", "source", "kind": "ink", "noncommercial" }. Files already in the folder from another export
 * are left; lines.jsonl and dataset.json are written anew.
 *
 * Any thread (no Qt GUI objects but QImage).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QStringList>

class Document;

namespace xqt::hwr {

struct LineInput;

struct LineExport {
    QString document;  ///< a .xopp, or a PDF with notes
    QString out;       ///< the dataset's folder (made if missing)
    QString texts;     ///< the transcripts ("": none)
    QString language = QStringLiteral("en");
    QString writer = QStringLiteral("me");
    QString licence = QStringLiteral("private");  ///< (the writer's own: not for a published model unless they say so)
    bool noncommercial = true;
};

struct LineExportResult {
    bool ok = false;
    int lines = 0;        ///< lines of ink written
    int transcripts = 0;  ///< lines of text in the transcript file
    QString error;        ///< why it failed
    QStringList warnings; ///< (the numbers of lines and texts differ)
};

/// The transcripts of a file as matched to the lines (empty lines and "#" lines skipped, NFC).
QStringList transcriptsOf(const QByteArray& text);

LineExportResult exportLines(const LineExport& job);
/// The same for a document in memory (`job.document` only names it: the ids and the source).
LineExportResult exportLines(const LineExport& job, Document& document);

/// The pieces of a dataset (also the forms', FormDataset.h): a line's picture, its ink as strokes/<id>.json holds it, a
/// name of letters, digits and "_" only, and a file written whole or not at all.
QImage lineImage(const LineInput& line);
QJsonObject strokesJson(const LineInput& line);
QString safeName(const QString& s);
bool writeDatasetFile(const QString& path, const QByteArray& data);

}  // namespace xqt::hwr
