/*
 * xournal-qt: pictures brought into a Markdown text (qt/docs/md-images.md, "Adding images"): pasted, dropped or
 * picked, they are saved where the document keeps its pictures ("name.assets/" next to a .md; the app cache of a PDF
 * text document) and linked as "name.assets/file".
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <string>

#include <QDateTime>
#include <QImage>
#include <QString>

#include "filesystem.h"

class QMimeData;

namespace xqt {
class DocumentSession;
}

namespace xqt::MarkdownImages {

/// Where a document keeps its pictures, and how links to them start ("name.assets/").
struct Place {
    fs::path folder;
    std::string prefix;
};
/// nullopt: the document keeps none (e.g. Markdown in a notes document not saved yet).
std::optional<Place> placeOf(const DocumentSession& session);

/// A name for a pasted picture that is free in the folder: "image-YYYY-MM-DD-HHMMSS.png" (Typora's), "…-2.png" if
/// taken.
std::string newName(const fs::path& folder, const std::string& extension, const QDateTime& now);
/// A name for a file brought in: its own name (made safe), "name (2).ext" if another file has it.
std::string freeName(const fs::path& folder, const std::string& name);
/// The link to a file of the folder as written in Markdown: the prefix and the name, with what Markdown or a web
/// address would read otherwise ("a b.png": "a%20b.png") %-encoded.
std::string linkFor(const Place& place, const std::string& name);
/// A picture's file name by its extension (png, jpg, jpeg, gif, webp, svg, bmp).
bool isPictureName(const QString& name);

/// The Markdown of a picture: "![alt](link)".
std::string markdownFor(const std::string& link, const std::string& alt = {});

/// Save a picture (the clipboard's) as a PNG: its link, or nullopt with `error`.
std::optional<std::string> savePicture(const DocumentSession& session, const QImage& image, QString& error,
                                       const QDateTime& now = QDateTime::currentDateTime());
/// Copy a picture file there (`source`: what QFile opens, `name`: its file name): its link, or nullopt with `error`.
/// A file that is in the folder already is linked as it is.
std::optional<std::string> addPictureFile(const DocumentSession& session, const QString& source, const QString& name,
                                          QString& error);

/// What a paste of `mime` into the Markdown of the document inserts when it holds pictures: their Markdown (saved
/// already), one per line; nullopt when it is no picture (then its text is pasted), an empty string with `error`
/// when it could not be saved. Pictures are copied files of pictures, or a picture without text (a copied text that
/// also carries a picture, as a spreadsheet's cells do, stays text).
std::optional<std::string> pastedPictures(const DocumentSession& session, const QMimeData* mime, QString& error);

}  // namespace xqt::MarkdownImages
