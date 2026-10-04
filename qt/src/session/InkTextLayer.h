/*
 * xournal-qt: the recognised handwriting as invisible text in the PDFs the app writes (qt/docs/handwriting-search.md),
 * so other PDF viewers find the words too.
 *
 * A PDF with notes (hybrid) or an archive PDF gets, per page with recognised handwriting, one content stream before
 * the page's own content: "q <placement> cm BT 3 Tr ... ET Q", the best reading of each word as text in render mode 3
 * (neither filled nor stroked: invisible, but found, selected and copied by viewers), over the word's box (the font
 * size is the box's height, the baseline a fifth above its bottom, Tz stretches it to the box's width; a space after
 * each word). Only readings safe enough
 * for viewers go in (MIN_P of the recogniser's guesses and MIN_CONF): a viewer cannot weigh them, and a wrong word
 * found there is worse than one not found. The other readings are only searched in the app.
 *
 * The font: a Type0 font (Identity-H, the codes are the UTF-16 code units of the text), a CIDFontType2 whose program is
 * a glyphless TrueType font made here (2 empty glyphs, every code mapped to glyph 1 by /CIDToGIDMap, advance 500/1000:
 * no .notdef, as PDF/A asks), and a /ToUnicode map back to the text. Characters beyond the BMP are left out.
 *
 * The stream carries our key (/XournalQt << /InkText (sig) >>): opening the file again removes it (the clean copy
 * has no text layer, the app searches the handwriting itself), and an incremental save writes only pages whose
 * stream changed (the marker's /InkText lists each page's sig).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QRectF>
#include <QString>

#include "InkText.h"

namespace xqt::InkTextLayer {

constexpr float MIN_P = 0.25f;
constexpr float MIN_CONF = 0.3f;
/// The font's name in a page's resources.
constexpr const char* FONT_RESOURCE = "/XqtInkText";

struct Word {
    QString text;
    QRectF box;  ///< page points, y down
};
/// The words of a page that go into its text layer.
std::vector<Word> wordsOf(const ink::PageText& page);
/// Per document page (null: none).
using Pages = std::vector<std::shared_ptr<const ink::PageText>>;

/// The content stream for these words on a page `pageHeight` high; `cm`: the placement on the base page (six numbers
/// as qpdf's QPDFMatrix::unparse() writes them, "" for none).
std::string contentOf(const std::vector<Word>& words, double pageHeight, const std::string& cm);
/// A short hash of a content stream (its sig).
std::string sigOf(const std::string& content);

/// The glyphless TrueType program, the /CIDToGIDMap (every code to glyph 1) and the /ToUnicode CMap.
const std::string& glyphlessFont();
const std::string& cidToGidMap();
const std::string& toUnicode();
/// The advance of every glyph, the font's ascent and descent (1/1000 of the size): a word's box is the line from the
/// descent to the ascent.
constexpr int ADVANCE = 500;
constexpr int ASCENT = 800;
constexpr int DESCENT = -200;

}  // namespace xqt::InkTextLayer
