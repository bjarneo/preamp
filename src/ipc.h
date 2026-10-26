#pragma once

// One JSON object per line over a Unix socket in XDG_RUNTIME_DIR. The window,
// the CLI and the Omarchy shell plugin all speak it. The daemon serves it.
//
//   client → daemon   {"cmd": "applyPreset", "name": "Vocal", "id": 7}
//   daemon → client   {"type": "reply", "id": 7, "ok": true}
//                     {"type": "state", ...}       after "subscribe"
//                     {"type": "spectrum", "l": [...]} after "subscribe" with spectrum

#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <functional>

namespace ipc {

QString socketPath();
QByteArray encode(const QJsonObject &message);

// The daemon's side.
class Connection : public QObject {
    Q_OBJECT
public:
    explicit Connection(QLocalSocket *socket, QObject *parent = nullptr);
    void send(const QJsonObject &message);
    bool wantsState = false;
    bool wantsSpectrum = false;

signals:
    void message(const QJsonObject &message);
    void closed();

private:
    QLocalSocket *m_socket;
    QByteArray m_buffer;
};

class Server : public QObject {
    Q_OBJECT
public:
    explicit Server(QObject *parent = nullptr);
    ~Server() override;
    bool listen(QString *error);
    QList<Connection *> connections() const { return m_connections; }

signals:
    void message(ipc::Connection *from, const QJsonObject &message);
    void subscribersChanged();

private:
    QLocalServer m_server;
    QList<Connection *> m_connections;
};

// The window's and the CLI's side. Reconnects on its own when asked to.
class Client : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonObject &)>;

    explicit Client(QObject *parent = nullptr);
    ~Client() override;
    void connectToDaemon();
    bool isConnected() const { return m_socket.state() == QLocalSocket::ConnectedState; }
    bool waitForConnected(int msecs) { return m_socket.waitForConnected(msecs); }
    void send(QJsonObject message, Reply reply = {});
    // Blocks until the reply arrives, for the CLI.
    QJsonObject request(const QJsonObject &message, int msecs = 5000);

signals:
    void connected();
    void disconnected();
    void message(const QJsonObject &message);

private:
    void read();

    QLocalSocket m_socket;
    QByteArray m_buffer;
    int m_nextId = 1;
    QHash<int, Reply> m_pending;
};

} // namespace ipc
