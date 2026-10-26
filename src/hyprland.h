#pragma once

// Follows Hyprland's event socket for the focused window and whether it is
// fullscreen, which the "app fullscreen" auto-switch rule matches on.

#include <QLocalSocket>
#include <QObject>
#include <QTimer>

class Hyprland : public QObject {
    Q_OBJECT

public:
    explicit Hyprland(QObject *parent = nullptr);
    ~Hyprland() override;
    bool available() const { return !m_path.isEmpty(); }
    QString activeClass() const { return m_class; }
    QString activeTitle() const { return m_title; }
    bool fullscreen() const { return m_fullscreen; }

signals:
    void changed();

private:
    void connectSocket();
    void read();

    QString m_path;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    QTimer m_retry;
    QString m_class, m_title;
    bool m_fullscreen = false;
};
