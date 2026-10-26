#include "ipc.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTimer>

namespace ipc {

namespace {
// A line longer than this is not something any client sends.
constexpr qsizetype MaxLine = 1 << 20;
} // namespace

QString socketPath() {
    if (qEnvironmentVariableIsSet("PREAMP_SOCKET"))
        return qEnvironmentVariable("PREAMP_SOCKET");
    QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + "/preamp.sock";
}

QByteArray encode(const QJsonObject &message) {
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

Connection::Connection(QLocalSocket *socket, QObject *parent) : QObject(parent), m_socket(socket) {
    socket->setParent(this);
    connect(socket, &QLocalSocket::readyRead, this, [this] {
        m_buffer += m_socket->readAll();
        if (m_buffer.size() > MaxLine && !m_buffer.contains('\n')) {
            m_socket->abort();
            return;
        }
        qsizetype nl;
        while ((nl = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(nl).trimmed();
            m_buffer.remove(0, nl + 1);
            if (line.isEmpty())
                continue;
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject())
                emit message(doc.object());
        }
    });
    connect(socket, &QLocalSocket::disconnected, this, &Connection::closed);
}

void Connection::send(const QJsonObject &message) {
    if (m_socket->state() != QLocalSocket::ConnectedState)
        return;
    // A client that stops reading must not grow the daemon without bound.
    if (m_socket->bytesToWrite() > 4 * MaxLine) {
        m_socket->abort();
        return;
    }
    m_socket->write(encode(message));
}

Server::Server(QObject *parent) : QObject(parent) {
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            auto *c = new Connection(socket, this);
            m_connections << c;
            connect(c, &Connection::message, this, [this, c](const QJsonObject &m) { emit message(c, m); });
            connect(c, &Connection::closed, this, [this, c] {
                m_connections.removeAll(c);
                c->deleteLater();
                emit subscribersChanged();
            });
        }
    });
}

Server::~Server() {
    // Connections are children, which Qt deletes after the members of this
    // object. Remove their close handlers first, so they cannot run against a
    // list that is already gone.
    for (Connection *c : std::as_const(m_connections)) {
        disconnect(c, nullptr, this, nullptr);
        delete c;
    }
    m_connections.clear();
    m_server.close();
}

bool Server::listen(QString *error) {
    const QString path = socketPath();
    // A socket left by a daemon that crashed is stale. A socket that answers
    // belongs to a daemon that is still running.
    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(300)) {
        *error = QStringLiteral("preamp is already running");
        return false;
    }
    QLocalServer::removeServer(path);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(path)) {
        *error = m_server.errorString();
        return false;
    }
    return true;
}

Client::Client(QObject *parent) : QObject(parent) {
    connect(&m_socket, &QLocalSocket::connected, this, &Client::connected);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        m_pending.clear();
        m_buffer.clear();
        emit disconnected();
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (m_socket.state() == QLocalSocket::UnconnectedState)
            emit disconnected();
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &Client::read);
}

// The socket is the first member, so it closes last and would report the
// close to handlers whose state is already gone.
Client::~Client() {
    disconnect(&m_socket, nullptr, this, nullptr);
}

void Client::connectToDaemon() {
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        return;
    m_socket.connectToServer(socketPath());
}

void Client::send(QJsonObject message, Reply reply) {
    if (reply) {
        const int id = m_nextId++;
        message.insert("id", id);
        m_pending.insert(id, std::move(reply));
    }
    m_socket.write(encode(message));
}

QJsonObject Client::request(const QJsonObject &message, int msecs) {
    QJsonObject result;
    bool done = false;
    QEventLoop loop;
    send(message, [&](const QJsonObject &r) {
        result = r;
        done = true;
        loop.quit();
    });
    QTimer::singleShot(msecs, &loop, &QEventLoop::quit);
    connect(this, &Client::disconnected, &loop, &QEventLoop::quit);
    if (!done)
        loop.exec();
    if (!done)
        result = {{"ok", false}, {"error", "preamp did not answer"}};
    return result;
}

void Client::read() {
    m_buffer += m_socket.readAll();
    qsizetype nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue;
        const QJsonObject obj = doc.object();
        if (obj.value("type").toString() == "reply") {
            const int id = obj.value("id").toInt();
            if (Reply r = m_pending.take(id))
                r(obj);
            continue;
        }
        emit message(obj);
    }
}

} // namespace ipc
