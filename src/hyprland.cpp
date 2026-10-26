#include "hyprland.h"

#include <QFileInfo>

Hyprland::Hyprland(QObject *parent) : QObject(parent) {
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (signature.isEmpty() || runtime.isEmpty())
        return;
    m_path = runtime + "/hypr/" + signature + "/.socket2.sock";
    m_retry.setSingleShot(true);
    m_retry.setInterval(3000);
    connect(&m_retry, &QTimer::timeout, this, &Hyprland::connectSocket);
    connect(&m_socket, &QLocalSocket::readyRead, this, &Hyprland::read);
    connect(&m_socket, &QLocalSocket::disconnected, &m_retry, qOverload<>(&QTimer::start));
    connect(&m_socket, &QLocalSocket::errorOccurred, &m_retry, qOverload<>(&QTimer::start));
    connectSocket();
}

Hyprland::~Hyprland() {
    disconnect(&m_socket, nullptr, nullptr, nullptr);
}

void Hyprland::connectSocket() {
    if (m_socket.state() == QLocalSocket::UnconnectedState && QFileInfo::exists(m_path))
        m_socket.connectToServer(m_path, QIODevice::ReadOnly);
    else if (m_socket.state() == QLocalSocket::UnconnectedState)
        m_retry.start();
}

void Hyprland::read() {
    m_buffer += m_socket.readAll();
    bool touched = false;
    qsizetype nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_buffer.left(nl));
        m_buffer.remove(0, nl + 1);
        const int sep = line.indexOf(">>");
        if (sep < 0)
            continue;
        const QString event = line.left(sep);
        const QString data = line.mid(sep + 2);
        if (event == "activewindow") {
            m_class = data.section(',', 0, 0);
            m_title = data.section(',', 1);
            touched = true;
        } else if (event == "fullscreen") {
            m_fullscreen = data.trimmed() == "1";
            touched = true;
        }
    }
    if (m_buffer.size() > 1 << 16)
        m_buffer.clear();
    if (touched)
        emit changed();
}
