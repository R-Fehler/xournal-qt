/*
 * xournal-qt: a filled handwriting form as an end-to-end benchmark of the handwriting search
 * (qt/research/hwr/forms/DESIGN.md, "From a filled form to data and numbers"). `xournal-qt-cli hwr-bench` runs it.
 *
 * The app's own pipeline reads every page of the form whole, without looking at the boxes: the layout (InkLayout.h,
 * lines at an angle included), each line as its recognisers get it (LineInput, LineImage.h), the recognisers, the
 * page's handwriting put together (ink::PageText) and the search's matching (ink::find). Only then are the results
 * compared with the manifest (FormManifest.h), box by box:
 *  - lines: the layout's lines whose middle is in the box (in its turned frame); a box should hold one, written at
 *    the box's angle (within ANGLE_TOLERANCE degrees);
 *  - CER: the best readings of the words whose middle is in the box (in reading order, joined by spaces) against the
 *    box's text (Levenshtein distance over the text's length);
 *  - words found: each word of 3 or more letters of the box's text is found among the readings of those words as the
 *    plain search finds handwriting (ink::find: a reading containing it or with one typo in words of 5+ letters; the
 *    best reading or one with MIN_P of the guesses), the measure of qt/research/hwr/train evaluate.py;
 *  - search recall: each of the box's `search` words searched on the whole page (ink::find) has a hit in the box;
 *  - text in drawings: words read inside `drawing` and `mark` boxes (there should be none);
 *  - false hits: the other boxes' search words of the page that hit words of this box (not words of its own text),
 *    over how many were tried.
 * Boxes for text that were left empty (no stroke is theirs, FormManifest.h: assign()) are skipped and counted. The
 * numbers are summed per section, kind, size (x_height_mm), angle and overall, for each recogniser side by side, with
 * the time the layout and each recogniser took. The report is JSON (with every box) and a Markdown summary.
 *
 * The recognisers are called one line after the other on the caller's thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <vector>

#include <QJsonObject>
#include <QString>

class Document;

namespace xqt::hwr {

class Recognizer;
struct FormManifest;

/// A line counts as written at its box's angle within this many degrees.
constexpr double ANGLE_TOLERANCE = 10;

struct BenchModel {
    QString name;  ///< the report's column
    std::shared_ptr<Recognizer> recognizer;
    QString folder;  ///< (the report names it)
};

struct BenchReport {
    bool ok = false;
    QString error;
    QJsonObject json;  ///< form, source, models, timing, groups ("all", "section:B", "kind:line", "size:3mm", "angle:90"), boxes
    QString markdown;
};

BenchReport runBench(Document& document, const FormManifest& manifest, const std::vector<BenchModel>& models,
                     const QString& source);

/// The Levenshtein distance of two texts (by UTF-16 unit; the CER's edits).
int editsBetween(const QString& a, const QString& b);

}  // namespace xqt::hwr
