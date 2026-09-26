/*
 * xournal-qt: renaming the document of a tab (qt/docs/library.md, "Renaming"): the tab strip (a double click on the
 * current tab's title, "Rename…" in its menu), "Rename…" in the ⋮ menu, the overview of open documents. It is the
 * library's rename: DocumentFiles::rename (a .xopp with its PDF, a .md with its pictures folder), then the search
 * index, the reading places and previews, the open tabs, the recent list and the links follow, as after a rename in
 * the library. A new document without a file gets the name it is saved under.
 *
 * @license GNU GPLv2 or later
 */
#include <QVariantMap>

#include "AppController.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentImages.h"
#include "session/DocumentSession.h"
#include "shell/DocumentFiles.h"
#include "shell/LibraryModel.h"
#include "shell/TabManager.h"

using namespace xqt;

namespace {
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }

/// What a tab's document is renamed as: its item, and its name split into what is typed and the extension that stays
/// (a text or other file is known by its whole file name; a document by its name without extension).
struct TabName {
    DocumentItem item;
    QString name;
    QString extension;
    bool unsaved = false;
    bool wholeName = false;  ///< a text or other file: DocumentFiles::rename takes the name with the extension
};

TabName tabNameOf(const DocumentSession& s) {
    TabName t;
    const fs::path file = s.documentFile();
    if (file.empty()) {
        t.unsaved = true;
        t.extension = QStringLiteral(".xopp");
        t.name = QString::fromStdString(s.untitledName().empty() ? s.getDisplayName() : s.untitledName());
        if (t.name.endsWith(t.extension)) {
            t.name.chop(t.extension.size());
        }
        return t;
    }
    t.item = DocumentFiles::itemOf(file, DocumentFiles::AllFiles);
    if (!t.item.other.empty() && t.item.xopp.empty() && t.item.pdf.empty() && t.item.md.empty() &&
        t.item.image.empty()) {
        const QString whole = QString::fromStdString(t.item.other.filename().string());
        const qsizetype dot = whole.lastIndexOf('.');
        t.wholeName = true;
        t.name = dot > 0 ? whole.left(dot) : whole;
        t.extension = dot > 0 ? whole.mid(dot) : QString();
        return t;
    }
    t.name = QString::fromStdString(t.item.valid() ? t.item.name() : file.stem().string());
    t.extension = QString::fromStdString(file.extension().string());  // (the tab's own file: .xopp, .pdf, .md, …)
    return t;
}

/// The whole new name as DocumentFiles::rename takes it
std::string renameTarget(const TabName& t, const QString& typed) {
    const QString name = typed.trimmed();
    return (t.wholeName ? name + t.extension : name).toStdString();
}
}  // namespace

QString AppController::renameProblemText(int problem, const QString& name) {
    switch (static_cast<DocumentFiles::RenameProblem>(problem)) {
        case DocumentFiles::RenameProblem::None:
            return {};
        case DocumentFiles::RenameProblem::Empty:
            return tr("The name cannot be empty.");
        case DocumentFiles::RenameProblem::Separator:
            return tr("A name cannot contain / or \\.");
        case DocumentFiles::RenameProblem::Invalid:
            return tr("“%1” cannot be used as a name.").arg(name);
        case DocumentFiles::RenameProblem::Taken:
            return tr("“%1” is taken: there is a document of this name here.").arg(name);
        case DocumentFiles::RenameProblem::ReadOnly:
            return tr("The file is read-only: it cannot be renamed.");
        case DocumentFiles::RenameProblem::Missing:
            return tr("The file is not there any more.");
    }
    return {};
}

QVariantMap AppController::tabRenameInfo(int index) const {
    DocumentSession* s = tabs->session(index);
    if (!s) {
        return {};
    }
    const TabName t = tabNameOf(*s);
    QString note;
    QString problem;
    if (t.unsaved) {
        note = tr("Not saved yet: the name it gets when it is saved.");
    } else if (!t.item.valid()) {
        problem = renameProblemText(static_cast<int>(DocumentFiles::RenameProblem::Missing), t.name);
    } else {
        const DocumentItem& i = t.item;
        if (!i.xopp.empty() && !i.pdf.empty()) {
            note = i.hybrid ? tr("Its copy for Xournal++ (%1) is renamed with it.").arg(qstr(i.xopp.filename()))
                            : tr("The Xournal file and its PDF (%1) are renamed together.")
                                      .arg(qstr(s->documentFile() == i.pdf ? i.xopp.filename() : i.pdf.filename()));
        } else if (!i.xopp.empty() && !i.image.empty()) {
            note = tr("The notes and the image (%1) are renamed together.").arg(qstr(i.image.filename()));
        } else if (!i.md.empty() && fs::is_directory(DocumentImages::assetsFolder(i.md))) {
            note = tr("Its pictures (%1) are renamed with it; its links to them follow.")
                           .arg(qstr(DocumentImages::assetsFolder(i.md).filename()));
        }
        if (!DocumentFiles::renamable(i)) {
            problem = renameProblemText(static_cast<int>(DocumentFiles::RenameProblem::ReadOnly), t.name);
        }
    }
    return {{"name", t.name}, {"extension", t.extension}, {"unsaved", t.unsaved}, {"note", note}, {"problem", problem}};
}

QString AppController::tabRenameProblem(int index, const QString& name) const {
    DocumentSession* s = tabs->session(index);
    if (!s) {
        return {};
    }
    const TabName t = tabNameOf(*s);
    const QString typed = name.trimmed();
    if (t.unsaved) {
        return renameProblemText(static_cast<int>(DocumentFiles::nameProblem(typed.toStdString())), typed);
    }
    const std::string target = renameTarget(t, typed);
    const auto problem = DocumentFiles::renameProblem(t.item, target);
    if (problem == DocumentFiles::RenameProblem::None && typed != t.name && s->isSaving()) {
        return tr("It is being saved: try again in a moment.");
    }
    return renameProblemText(static_cast<int>(problem), QString::fromStdString(target));
}

bool AppController::renameTab(int index, const QString& name) {
    DocumentSession* s = tabs->session(index);
    if (!s) {
        return false;
    }
    if (const QString problem = tabRenameProblem(index, name); !problem.isEmpty()) {
        Q_EMIT message(tr("Cannot rename"), problem, true);
        return false;
    }
    const TabName t = tabNameOf(*s);
    if (t.unsaved) {
        s->setUntitledName(name.trimmed().toStdString());
        Q_EMIT titleChanged();
        return true;
    }
    if (name.trimmed() == t.name) {
        return true;  // (nothing to do)
    }
    const auto r = DocumentFiles::rename(t.item, renameTarget(t, name));
    if (!r.ok) {
        Q_EMIT message(tr("Cannot rename"), QString::fromStdString(r.error), true);
        return false;
    }
    // What a rename in the library does after it: the search index, reading places and previews (followMoves),
    // then the open tabs, the recent list and the links (filesChanged)
    library->filesMoved(r);
    filesChanged(r);
    return true;
}

QString AppController::renameProblem(const QString& path, const QString& name) const {
    const fs::path file(path.toStdString());
    const std::string typed = name.trimmed().toStdString();
    std::error_code ec;
    const auto problem =
            fs::is_directory(file, ec)
                    ? DocumentFiles::folderRenameProblem(file, typed)
                    : DocumentFiles::renameProblem(DocumentFiles::itemOf(file, DocumentFiles::AllFiles), typed);
    return renameProblemText(static_cast<int>(problem), name.trimmed());
}

void AppController::followShownFile(DocumentSession& s, const fs::path& from, const fs::path& to) {
    const fs::path old = s.shownFile();
    const fs::path now = DocumentFiles::remap(old, from, to);
    if (now == old) {
        return;
    }
    Document* doc = s.getDocument();
    doc->lock();
    for (size_t i = 0; i < doc->getPageCount(); ++i) {
        PageRef page = doc->getPage(i);
        BackgroundImage& bg = page->getBackgroundImage();
        if (page->getBackgroundType().isImagePage() && !bg.isAttached() && bg.getFilepath() == old) {
            bg.setFilepath(now);
        }
    }
    doc->unlock();
    s.setShownFile(now, s.isReadOnly());
}
