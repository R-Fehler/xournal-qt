#include "SingleInstance.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

#include <unistd.h>

namespace xqt {

SingleInstance::SingleInstance(QString key, QObject* parent): QObject(parent), key(std::move(key)) {
    if (this->key.isEmpty()) {
        this->key = QString("xournal-qt-%1").arg(getuid());
    }
}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::sendToRunningInstance(const QStringList& paths, int timeoutMs) {
    QLocalSocket socket;
    socket.connectToServer(key);
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << paths;
    socket.write(payload);
    socket.flush();
    socket.waitForBytesWritten(timeoutMs);
    // Wait for the acknowledgement so that the files are not lost if the other instance is just quitting.
    const bool ok = socket.waitForReadyRead(timeoutMs) && socket.readAll().startsWith("ok");
    socket.disconnectFromServer();
    return ok;
}

bool SingleInstance::listen() {
    server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(key)) {
        // A stale socket from a crashed instance: nobody answered sendToRunningInstance(), so take it over.
        QLocalServer::removeServer(key);
        if (!server->listen(key)) {
            return false;
        }
    }
    connect(server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* socket = server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                QDataStream in(socket);
                in.startTransaction();
                QStringList paths;
                in >> paths;
                if (!in.commitTransaction()) {
                    return;  // wait for the rest of the message
                }
                socket->write("ok");
                socket->flush();
                Q_EMIT filesRequested(paths);
            });
        }
    });
    return true;
}

}  // namespace xqt
