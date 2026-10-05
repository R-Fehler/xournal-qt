/*
 * xournal-qt: tags (qt/docs/tags.md): `#tag` in what is typed, and keywords of PDFs.
 *
 * A tag is written `#name`: letters, digits, `-`, `_` and `/` for nested tags (`#course/math` is inside `#course`),
 * with at least one letter (`#3`, `#1984` are none). The `#` starts the text or follows a space or an opening mark
 * (`(`, `[`, a quote, ...): a URL's fragment (`page#section`), `C#` and a Markdown heading (`# Title`: a space after
 * the `#`) are none. In Markdown, code (inline and blocks), formulas and HTML are left out, and an Obsidian front
 * matter's `tags:` (a list, `[a, b]`, or words) counts too.
 *
 * Keywords of a PDF (its document information's /Keywords, its XMP metadata's dc:subject) are tags as well: split at
 * commas or semicolons (without any, at spaces), each made a tag (a leading `#` dropped, other characters than those
 * of a tag become `-`).
 *
 * Tags are compared without case (`#Exam` is `#exam`); the spelling shown is the one found first.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string_view>

#include <QString>
#include <QStringList>
#include <QStringView>

namespace xqt::tags {

/// The tags of a plain text (a typed text element), in order, each once.
QStringList inText(QStringView text);
/// The tags of a Markdown text (UTF-8): its text without code, formulas and HTML, and its front matter's `tags:`.
QStringList inMarkdown(std::string_view source);
/// The tags of an Obsidian front matter (`---` … `---` at the start), "tags:" or "tag:".
QStringList inFrontMatter(std::string_view source);

/// A keyword as a tag ("" if it cannot be one): "Machine learning" → "Machine-learning", "#exam" → "exam".
QString fromKeyword(QStringView keyword);
/// The tags of a keywords string ("physics, optics; exam" or "physics optics").
QStringList fromKeywords(QStringView keywords);

/// What tags are compared by (case folded).
QString key(QStringView tag);
/// `more` added to `into` where not there yet (case ignored).
void merge(QStringList& into, const QStringList& more);
/// Whether `list` has `tag` (case ignored).
bool contains(const QStringList& list, QStringView tag);

/// A query for a tag (`tag:course`, a tab's choice): the tag itself and the tags inside it (`course` finds `course`
/// and `course/math`); ending in `/`, only those inside it (`course/` finds `course/math`). A `#` before it is
/// ignored, and the case.
bool matches(QStringView tag, QStringView query);
/// Whether any of `list` matches `query`.
bool anyMatches(const QStringList& list, QStringView query);

/// The `tag:name` terms of a plain search, and what is left of it ("tag:exam kalman" → {"exam"}, "kalman").
struct Query {
    QStringList tags;
    QString rest;
};
Query splitQuery(const QString& query);

/// The longest a tag is.
constexpr int MAX_LENGTH = 100;

}  // namespace xqt::tags
