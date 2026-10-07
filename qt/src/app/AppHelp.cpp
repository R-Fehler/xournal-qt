/*
 * xournal-qt: Help (qt/docs/onboarding.md). The tutorial is a Markdown text shipped in the resources
 * (qt/resources/help/tutorial.md, its pictures in tutorial.assets/). Help → Tutorial opens a copy of it to write on:
 * a PDF text document (qt/docs/md-pdf.md) in the app's data folder, not in a library, so the user's folders stay clean
 * and the copy is there next time with what was written on it. Save as… puts it into a library when the user wants it
 * there. "Start the tutorial again" replaces the copy with a fresh one.
 *
 * When the author ships a finished tutorial.pdf (a PDF with notes: the text with ink written on it in the app) next to
 * tutorial.md, the copy is that PDF instead.
 *
 * @license GNU GPLv2 or later
 */
#include <string>
#include <system_error>

#include <QFile>
#include <QStandardPaths>

#include "session/DocumentImages.h"
#include "session/DocumentSession.h"
#include "session/FileIo.h"
#include "shell/TabManager.h"

#include "AppController.h"

using namespace xqt;

namespace {
/// Where the tutorial and its pictures are in the resources
const QString RESOURCES = QStringLiteral(":/xqt-help/");

/// A file of the resources written to `to` (a copy that can be written: a resource file copied by QFile::copy is
/// read-only)
bool writeResource(const QString& name, const fs::path& to) {
    QFile in(RESOURCES + name);
    if (!in.open(QIODevice::ReadOnly)) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    return fileio::writeFileAtomically(QString::fromStdString(to.string()), in.readAll());
}
}  // namespace

QString AppController::tutorialResource() { return RESOURCES + QStringLiteral("tutorial.md"); }

QString AppController::tutorialFile() const {
    const fs::path folder(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString());
    return QString::fromStdString((folder / "Tutorial" / "Tutorial.pdf").string());
}

bool AppController::tutorialExists() const {
    std::error_code ec;
    return fs::exists(fs::path(tutorialFile().toStdString()), ec);
}

bool AppController::openTutorial() {
    const QString file = tutorialFile();
    if (tutorialExists()) {
        return openPath(file);  // (with what was written on it; the tab if it is open)
    }
    const fs::path pdf(file.toStdString());
    // A finished tutorial shipped as a PDF with notes: a copy of it
    if (QFile::exists(RESOURCES + QStringLiteral("tutorial.pdf"))) {
        if (!writeResource(QStringLiteral("tutorial.pdf"), pdf)) {
            Q_EMIT message(tr("Cannot open the tutorial"), tr("\"%1\" cannot be written.").arg(file), true);
            return false;
        }
        const bool opened = openPath(file);
        Q_EMIT tutorialChanged();
        return opened;
    }
    // The Markdown: a PDF text document made from it, its pictures packed into it (as Open as PDF document does)
    QFile md(tutorialResource());
    if (!md.open(QIODevice::ReadOnly)) {
        Q_EMIT message(tr("Cannot open the tutorial"), tr("The tutorial is missing from this build."), true);
        return false;
    }
    const std::string text = md.readAll().toStdString();
    const fs::path work = DocumentImages::workFolder(pdf);
    for (const std::string& carried: DocumentImages::carriedLinks(text)) {
        if (const fs::path to = DocumentImages::below(work, carried); !to.empty()) {
            writeResource(QString::fromStdString(carried), to);  // (a picture not shipped: shown as missing)
        }
    }
    std::error_code ec;
    fs::create_directories(pdf.parent_path(), ec);
    const bool made = makeTextPdf(text, pdf);
    Q_EMIT tutorialChanged();
    return made;
}

bool AppController::restartTutorial() {
    const fs::path pdf(tutorialFile().toStdString());
    if (const int index = tabs->indexOfFile(pdf); index >= 0) {
        if (DocumentSession* s = tabs->session(index); s && s->isSaving()) {
            Q_EMIT message(tr("The tutorial is being saved"), tr("Try again in a moment."), false);
            return false;
        }
        closeTab(index);  // (what was written on it goes: the window asked)
    }
    std::error_code ec;
    fs::remove(pdf, ec);
    Q_EMIT tutorialChanged();
    return openTutorial();
}
