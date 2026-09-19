/*
 * xournal-qt M0 spike: shared state for both hosts (model, viewport, input router, logging, HUD).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <QTimer>

#include "Diagnostics.h"
#include "InkModel.h"
#include "InputRouter.h"
#include "Viewport.h"

class SpikeContext: public QObject {
    Q_OBJECT
    Q_PROPERTY(QString hud READ hud NOTIFY hudChanged)
    Q_PROPERTY(QString environment READ environment CONSTANT)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
public:
    SpikeContext(QString host, const QString& logPath, QObject* parent = nullptr);
    static SpikeContext* instance() { return self; }

    InkModel* model() { return &ink; }
    Viewport* viewport() { return &view; }
    InputRouter* router() { return &input; }
    EventLog* log() { return &eventLog; }

    QString hud() const { return hudText; }
    QString environment() const { return env; }
    QString hostName() const { return host; }

    Q_INVOKABLE void clear() { ink.clear(); }
    Q_INVOKABLE void undo() { ink.undo(); }
    Q_INVOKABLE void redo() { ink.redo(); }
    Q_INVOKABLE void fitWidth() { view.fitWidth(); }

    /// Application-wide filter: logs input events once per event (at the QWindow level) and tracks proximity.
    bool eventFilter(QObject* watched, QEvent* event) override;

Q_SIGNALS:
    void hudChanged();

private:
    static inline SpikeContext* self = nullptr;
    QString host;
    QString env;
    EventLog eventLog;
    InkModel ink;
    Viewport view;
    InputRouter input;
    QTimer hudTimer;
    QString hudText;
};
