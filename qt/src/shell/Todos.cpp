#include "Todos.h"

#include <QCoreApplication>
#include <QFileInfo>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/TextFile.h"

#include "DocumentFiles.h"
#include "MdTasks.h"

namespace xqt::todos {

Rules Rules::of(Settings& settings) {
    Rules r;
    auto& part = settings.getCustomElement("xournalQt");
    std::string source;
    part.getString("todoSource", source);
    r.all = source == "all";
    std::string marker;
    if (part.getString("todoMarker", marker)) {
        r.marker = QString::fromStdString(marker).trimmed();
    }
    return r;
}

bool listed(const LibraryIndex::Todo& todo, const Rules& rules) {
    // (an empty marker: every check box, as there is nothing to look for)
    return todo.stamp || rules.all || rules.marker.isEmpty() || todo.text.contains(rules.marker, Qt::CaseInsensitive);
}

QString shownText(const QString& text, const QString& marker) {
    QString t = QString::fromStdString(md::tasks::withoutDueDate(text.toStdString()));
    if (!marker.isEmpty()) {
        for (qsizetype at = t.indexOf(marker, 0, Qt::CaseInsensitive); at >= 0;
             at = t.indexOf(marker, at, Qt::CaseInsensitive)) {
            t.remove(at, marker.size());
        }
    }
    return t.simplified();
}

std::optional<Place> find(Document& doc, const QString& text, int occurrence) {
    const std::string wanted = text.toStdString();
    int seen = 0;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        const PageRef page = doc.getPage(i);
        // (the order of md::boxesOf, as the index reads them)
        for (Layer* layer: page->getLayers()) {
            for (const Element* e: layer->getElementsView()) {
                if (e->getType() != ELEMENT_TEXT || !static_cast<const Text*>(e)->isMarkdown()) {
                    continue;
                }
                auto* box = const_cast<Text*>(static_cast<const Text*>(e));  // (the caller's document, locked)
                for (const md::tasks::Task& t: md::tasks::find(box->getText())) {
                    if (t.text == wanted && seen++ == occurrence) {
                        return Place{page, i, layer, box, t.mark, t.done};
                    }
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<size_t> find(const std::string& markdown, const QString& text, int occurrence) {
    const std::string wanted = text.toStdString();
    int seen = 0;
    for (const md::tasks::Task& t: md::tasks::find(markdown)) {
        if (t.text == wanted && seen++ == occurrence) {
            return t.mark;
        }
    }
    return std::nullopt;
}

namespace {
QString tr(const char* text) { return QCoreApplication::translate("Todos", text); }
}  // namespace

QString whyNotWritable(const fs::path& file, PdfKind kind) {
    const QFileInfo info(QString::fromStdString(file.string()));
    if (!info.exists()) {
        return tr("The file is gone.");
    }
    if (!info.isWritable()) {
        return tr("The file cannot be written (it is read-only).");
    }
    if (DocumentFiles::isMarkdownFile(file)) {
        return {};
    }
    const QString ext = info.suffix().toLower();
    if (ext == QLatin1String("xopp")) {
        return {};
    }
    if (ext == QLatin1String("pdf")) {
        switch (kind) {
            case PdfKind::Notes:
            case PdfKind::Text:
                return {};
            case PdfKind::Archive:
            case PdfKind::ArchiveText:
                return tr("An archive PDF is changed only when it is open: open it to tick the to-do.");
            default:
                return tr("This PDF has no notes of the app to change.");
        }
    }
    return tr("This kind of file is not changed here: open it to tick the to-do.");
}

bool setInMarkdownFile(const fs::path& file, const QString& text, int occurrence, bool done, bool& found,
                       std::string& error) {
    found = false;
    TextFile t;
    if (!t.load(file, TextFile::Kind::Markdown, error)) {
        return false;
    }
    if (!t.editable()) {
        error = tr("The file cannot be edited (not UTF-8, or too big).").toStdString();
        return false;
    }
    const auto mark = find(t.text(), text, occurrence);
    if (!mark) {
        return true;
    }
    found = true;
    const std::string changed = md::tasks::withTask(t.text(), *mark, done);
    return changed == t.text() || t.save(changed, error);
}

}  // namespace xqt::todos
