/*
 * xournal-qt: AppController, annotations of other apps made editable (qt/docs/adopt-annotations.md;
 * session/AdoptAnnotations.h).
 *
 * Opening a PDF (plain, or with notes) looks at its annotations on a worker; when other apps' marks can be made
 * editable the window asks once per file. ⋮ → Document and the Annotations panel offer it any time.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <shared_mutex>
#include <system_error>

#include <QCoreApplication>
#include <QPointer>
#include <QThread>

#include "AppController.h"
#include "AppServices.h"
#include "model/Document.h"
#include "session/AdoptAnnotations.h"
#include "session/DocumentSession.h"
#include "shell/DocumentPlaces.h"
#include "shell/TabManager.h"

using namespace xqt;

namespace {
fs::path backgroundOf(DocumentSession* s) {
    Document* doc = s->getDocument();
    std::shared_lock lock(*doc);
    return doc->getPdfFilepath();
}

}  // namespace

void AppController::scanAdoptable(DocumentSession* s, bool offer) {
    if (!s || s->isReadOnly() || s->textFile()) {
        return;
    }
    const fs::path bg = backgroundOf(s);
    if (bg.empty()) {
        return;
    }
    AdoptScan& known = adoptScans[s->serial()];
    known.offer = known.offer || offer;  // (a scan that runs asks when it is done)
    if ((known.pdf == bg && !offer) || known.scanning) {
        return;
    }
    const DocumentSession::AdoptPlan plan = s->planAdoption();
    if (!plan.ok) {
        return;
    }
    known.scanning = true;
    QPointer<AppController> self(this);
    QPointer<DocumentSession> session(s);
    const quint64 serial = s->serial();
    appServices->jobs().start([self, session, serial, plan] {
        const adopt::Scan scan = adopt::scan(plan.pdf, plan.pdfPages);
        QMetaObject::invokeMethod(qApp, [self, session, serial, plan, scan] {
            if (!self) {
                return;
            }
            AdoptScan& known = self->adoptScans[serial];
            known.scanning = false;
            if (!session) {
                self->adoptScans.erase(serial);
                return;
            }
            known.pdf = plan.pdf;
            known.count = scan.ok ? static_cast<int>(scan.count) : 0;
            known.app = QString::fromStdString(scan.app);
            if (session == self->session()) {
                Q_EMIT self->adoptableChanged();
            }
            const bool offer = known.offer;
            if (backgroundOf(session) != plan.pdf) {
                self->scanAdoptable(session, false);  // (it changed meanwhile)
                return;
            }
            known.offer = false;
            const fs::path file = session->documentFile();
            if (!offer || known.count == 0 || file.empty()) {
                return;
            }
            const fs::path key = DocumentPlaces::keyOf(file);
            if (known.count <= DocumentPlaces::adoptionOffered(key)) {
                return;  // (asked about these before)
            }
            DocumentPlaces::setAdoptionOffered(key, known.count);
            if (session == self->session()) {
                Q_EMIT self->annotationsToAdopt(known.count, known.app,
                                                QString::fromStdString(file.filename().string()));
            }
        });
    }, BackgroundJobs::Priority::Idle);
}

int AppController::adoptableCount() const {
    DocumentSession* s = session();
    if (!s) {
        return 0;
    }
    auto it = adoptScans.find(s->serial());
    return it != adoptScans.end() && it->second.pdf == backgroundOf(s) ? it->second.count : 0;
}

QString AppController::adoptableApp() const {
    DocumentSession* s = session();
    auto it = s ? adoptScans.find(s->serial()) : adoptScans.end();
    return it != adoptScans.end() ? it->second.app : QString();
}

void AppController::declineAdoption() {
    DocumentSession* s = session();
    if (!s || s->documentFile().empty()) {
        return;
    }
    const fs::path key = DocumentPlaces::keyOf(s->documentFile());
    DocumentPlaces::setAdoptionOffered(key, std::max(DocumentPlaces::adoptionOffered(key), adoptableCount()));
}

void AppController::adoptAnnotations() {
    DocumentSession* s = session();
    if (!s || adoptRunning) {
        return;
    }
    const QString title = tr("Adopt annotations");
    if (s->isReadOnly() || s->textFile()) {
        Q_EMIT message(title, tr("This document cannot be changed."), false);
        return;
    }
    const DocumentSession::AdoptPlan plan = s->planAdoption();
    if (!plan.ok) {
        Q_EMIT pageActionDone(tr("This document has no PDF pages with annotations of other apps."), false);
        return;
    }
    adoptRunning = true;
    Q_EMIT adoptableChanged();
    QPointer<AppController> self(this);
    QPointer<DocumentSession> session(s);
    appServices->jobs().start([self, session, plan, title] {
        auto prepared = std::make_shared<adopt::Prepared>(
                adopt::prepare(plan.pdf, plan.pdfPages, plan.copy,
                               plan.mark < 0 ? std::nullopt : std::optional(static_cast<MergedPdf::Kind>(plan.mark))));
        QMetaObject::invokeMethod(qApp, [self, session, plan, title, prepared] {
            if (!self || !session) {
                if (!prepared->copy.empty()) {
                    std::error_code ec;
                    fs::remove(prepared->copy, ec);
                }
                if (self) {
                    self->adoptRunning = false;
                    Q_EMIT self->adoptableChanged();
                }
                return;
            }
            self->adoptRunning = false;
            const size_t n = prepared->converted;
            const QString app = QString::fromStdString(prepared->app);
            std::string error;
            if (!session->applyAdoption(std::move(*prepared), plan, error)) {
                Q_EMIT self->message(title, QString::fromStdString(error), true);
            } else if (n == 0) {
                Q_EMIT self->pageActionDone(tr("No annotations of other apps to make editable."), false);
            } else {
                Q_EMIT self->pageActionDone(app.isEmpty() ?
                                                    tr("%n annotation(s) of another app made editable", "", int(n)) :
                                                    tr("%n annotation(s) from %1 made editable", "", int(n)).arg(app),
                                            true);
            }
            self->adoptScans.erase(session->serial());
            self->scanAdoptable(session, false);  // (what is left: none, or those it could not convert)
            Q_EMIT self->adoptableChanged();
        });
    }, BackgroundJobs::Priority::Idle);
}
