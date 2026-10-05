#include "VersionsModel.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <QThreadPool>
#include <QTimeZone>

#include "model/Document.h"
#include "session/DocumentSession.h"
#include "session/PdfEncryption.h"
#include "session/HybridPdf.h"
#include "session/VersionCache.h"

#include "PageClipboard.h"

namespace xqt {

namespace {
/// Reading the list: below the canvas and the thumbnails.
QThreadPool& pool() {
    static QThreadPool* p = [] {
        auto* tp = new QThreadPool;
        tp->setMaxThreadCount(1);
        tp->setThreadPriority(QThread::LowestPriority);
        return tp;
    }();
    return *p;
}

bool hasPdfExtension(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".pdf";
}

QString megabytes(uint64_t bytes) {
    return QLocale().toString(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', bytes < 10 * 1024 * 1024 ? 1 : 0) +
           QStringLiteral(" MB");
}
}  // namespace

VersionsModel::VersionsModel(QObject* parent): QAbstractListModel(parent) {}

VersionsModel::~VersionsModel() {
    for (auto& c: connections) {
        disconnect(c);
    }
}

void VersionsModel::setSession(DocumentSession* s) {
    for (auto& c: connections) {
        disconnect(c);
    }
    connections.clear();
    session = s;
    ++generation;  // (a read for the document before is not taken)
    listed = PdfHistory::Listed();
    if (s) {
        connections.push_back(connect(s, &DocumentSession::versionsChanged, this, &VersionsModel::refresh));
        connections.push_back(connect(s, &DocumentSession::savingChanged, this, [this](bool saving) {
            if (!saving) {
                refresh();
            }
        }));
        connections.push_back(connect(s, &DocumentSession::modifiedChanged, this, [this] { rebuild(); }));
        connections.push_back(connect(s, &DocumentSession::filePathChanged, this, &VersionsModel::refresh));
    }
    reading = false;
    Q_EMIT busyChanged();
    refresh();
}

void VersionsModel::setActive(bool on) {
    if (on == isActive) {
        return;
    }
    isActive = on;
    Q_EMIT activeChanged();
    if (on) {
        refresh();
    }
}

bool VersionsModel::available() const { return session && unavailableReason().isEmpty(); }

bool VersionsModel::needsPdf() const {
    return session && !session->textFile() && session->hasFilePath() && !hasPdfExtension(session->getFilePath());
}

QString VersionsModel::unavailableReason() const {
    if (!session) {
        return tr("No document is open.");
    }
    if (session->textFile()) {
        return tr("A text file keeps no versions.");
    }
    if (needsPdf()) {
        return tr("Version history needs a PDF with notes: the versions are kept inside the PDF, so they go wherever "
                  "the file goes.");
    }
    if (session->isHybrid()) {
        std::error_code ec;
        if (fs::exists(session->getFilePath(), ec) && HybridPdf::markerOf(session->getFilePath()).archive) {
            return tr("An archive PDF keeps no versions (it is meant to stay as it was exported).");
        }
    }
    if (session->hasOlderEncryption()) {
        return tr("This PDF is encrypted with an older method than AES-256, which this app writes only in full: it "
                  "keeps no versions. Protecting it with a password (⋮ → Document) encrypts it with AES-256.");
    }
    return {};
}

bool VersionsModel::on() const { return session && available() && session->keepsVersions(); }

void VersionsModel::setOn(bool on) {
    if (!session || !available()) {
        return;
    }
    session->setKeepsVersions(on);
    Q_EMIT changed();
}

bool VersionsModel::pending() const { return session && session->versionsChoicePending(); }

void VersionsModel::setMilestonesOnly(bool on) {
    if (on != onlyMilestones) {
        onlyMilestones = on;
        rebuild();
    }
}

void VersionsModel::refresh() {
    if (!session || !isActive) {
        rebuild();
        return;
    }
    std::error_code ec;
    const fs::path pdf = session->hasFilePath() ? session->getFilePath() : fs::path();
    if (pdf.empty() || !session->isHybrid() || !fs::exists(pdf, ec)) {
        listed = PdfHistory::Listed();
        rebuild();
        return;
    }
    if (reading) {
        again = true;
        return;
    }
    reading = true;
    Q_EMIT busyChanged();
    const unsigned gen = generation;
    QPointer<VersionsModel> self(this);
    pool().start([self, pdf, gen] {
        auto l = std::make_shared<PdfHistory::Listed>(PdfHistory::list(pdf));
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, gen, l] {
            if (!self) {
                return;
            }
            self->reading = false;
            if (gen == self->generation) {
                self->listed = std::move(*l);
            }
            Q_EMIT self->busyChanged();
            self->rebuild();
            if (self->again) {
                self->again = false;
                self->refresh();
            }
        });
    });
}

QString VersionsModel::titleFor(const std::string& isoUtc, std::time_t now) {
    QDateTime when = QDateTime::fromString(QString::fromStdString(isoUtc), Qt::ISODate);
    if (!when.isValid()) {
        return QString::fromStdString(isoUtc);
    }
    when = when.toLocalTime();
    const QDate today = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(now)).toLocalTime().date();
    const QString time = QLocale().toString(when.time(), QLocale::ShortFormat);
    if (when.date() == today) {
        return tr("Today %1").arg(time);
    }
    if (when.date() == today.addDays(-1)) {
        return tr("Yesterday %1").arg(time);
    }
    const QString day = when.date().year() == today.year() ? QLocale().toString(when.date(), QStringLiteral("d MMM"))
                                                           : QLocale().toString(when.date(), QStringLiteral("d MMM yyyy"));
    return day + QStringLiteral(" ") + time;
}

QString VersionsModel::titleOf(int id) const {
    for (const auto& v: listed.versions) {
        if (v.id == id) {
            return titleFor(v.date, PdfHistory::now());
        }
    }
    return {};
}

QString VersionsModel::messageOf(int id) const {
    for (const auto& v: listed.versions) {
        if (v.id == id) {
            return QString::fromStdString(v.message);
        }
    }
    return {};
}

void VersionsModel::rebuild() {
    beginResetModel();
    rows.clear();
    if (session && available()) {
        if (session->isModified() && (on() || !listed.versions.empty())) {
            Row r;
            r.kind = QStringLiteral("unsaved");
            r.title = tr("Unsaved changes");
            r.detail = on() ? tr("Saved as a version with the next save") : QString();
            rows.push_back(r);
        }
        // Versions and other apps' revisions by their place in the file, newest first
        struct Item {
            uint64_t end;
            Row row;
        };
        std::vector<Item> items;
        const std::time_t now = PdfHistory::now();
        for (size_t k = 0; k < listed.versions.size(); ++k) {
            const auto& v = listed.versions[k];
            if (onlyMilestones && !v.milestone()) {
                continue;
            }
            Row r;
            r.id = v.id;
            r.kind = QStringLiteral("version");
            r.title = titleFor(v.date, now);
            r.message = QString::fromStdString(v.message);
            r.milestone = v.milestone();
            r.current = k + 1 == listed.versions.size();
            if (v.kind == PdfHistory::Kind::RECEIVED) {
                r.detail = tr("The PDF as it was received");
            } else if (v.id == 0) {
                r.detail = tr("As it was before versions were kept");
            } else {
                r.detail = tr("%n page(s)", "", v.pages);
            }
            items.push_back({v.end, r});
        }
        if (!onlyMilestones) {
            for (const auto& o: listed.others) {
                Row r;
                r.kind = QStringLiteral("other");
                r.title = o.date.empty() ? tr("Changed in another app") : titleFor(o.date, now);
                r.message = tr("Changed in another app");
                items.push_back({o.end, r});
            }
        }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.end > b.end; });
        for (auto& i: items) {
            rows.push_back(std::move(i.row));
        }
    }
    endResetModel();
    Q_EMIT changed();
}

QString VersionsModel::sizeText() const {
    if (listed.versions.empty()) {
        return {};
    }
    const auto& first = listed.versions.front();
    const QString since = titleFor(first.date, PdfHistory::now());
    if (listed.size <= first.end) {
        return megabytes(listed.size);
    }
    return tr("%1 in all, %2 of it from the versions since %3")
            .arg(megabytes(listed.size), megabytes(listed.size - first.end), since);
}

int VersionsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows.size());
}

QVariant VersionsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows.size())) {
        return {};
    }
    const Row& r = rows[static_cast<size_t>(index.row())];
    switch (role) {
        case VersionIdRole:
            return r.id;
        case KindRole:
            return r.kind;
        case TitleRole:
            return r.title;
        case MessageRole:
            return r.message;
        case MilestoneRole:
            return r.milestone;
        case DetailRole:
            return r.detail;
        case CurrentRole:
            return r.current;
        default:
            return {};
    }
}

QHash<int, QByteArray> VersionsModel::roleNames() const {
    return {{VersionIdRole, "versionId"}, {KindRole, "kind"},           {TitleRole, "title"},
            {MessageRole, "message"},     {MilestoneRole, "milestone"}, {DetailRole, "detail"},
            {CurrentRole, "current"}};
}

bool VersionsModel::restore(int id) {
    if (!session || restoringNow || !session->isHybrid()) {
        return false;
    }
    const fs::path pdf = session->getFilePath();
    const QString title = titleOf(id);
    restoringNow = true;
    Q_EMIT busyChanged();
    QPointer<VersionsModel> self(this);
    QPointer<DocumentSession> target(session.data());
    // The version, loaded and its pages copied on a worker (its clean copy may take seconds for a long PDF)
    QThreadPool::globalInstance()->start([self, target, pdf, id, title] {
        std::string error;
        auto clip = std::make_shared<PageClipboard>();
        const fs::path file = VersionCache::instance().get(pdf, id, error);
        if (!file.empty()) {
            auto loaded = DocumentSession::loadFile(file, false, PdfEncryption::passwordOf(file));
            if (loaded.document) {
                std::vector<size_t> all(loaded.document->getPageCount());
                for (size_t i = 0; i < all.size(); ++i) {
                    all[i] = i;
                }
                clip->copy(*loaded.document, all, true);
            } else {
                error = loaded.error;
            }
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, target, clip, error, title] {
            if (!self) {
                return;
            }
            self->restoringNow = false;
            Q_EMIT self->busyChanged();
            if (!target || clip->isEmpty()) {
                Q_EMIT self->restored(false, error.empty() ? tr("The version could not be read.")
                                                           : QString::fromStdString(error));
                return;
            }
            const auto pages = clip->pagesFor(*target);
            const QString message = tr("Restored the version of %1").arg(title);
            target->replaceAllPages(pages, message.toStdString());
            target->versionRestored(message.toStdString());
            self->rebuild();
            Q_EMIT self->restored(true, message);
        });
    });
    return true;
}

}  // namespace xqt
