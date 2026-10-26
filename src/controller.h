#pragma once

// The window's link to the daemon. It mirrors the daemon's state for QML,
// sends what the user does, and smooths the analyzer for drawing.

#include "ipc.h"

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY connectedChanged)
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)
    Q_PROPERTY(bool spectrumWanted READ spectrumWanted WRITE setSpectrumWanted NOTIFY spectrumWantedChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    Q_PROPERTY(bool searchPending READ searchPending NOTIFY searchChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchChanged)
    Q_PROPERTY(QVariantList availablePlugins READ availablePlugins NOTIFY availablePluginsChanged)
    // Every preset with its bands, fetched again when the daemon's library
    // revision changes.
    Q_PROPERTY(QVariantList library READ library NOTIFY libraryChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

public:
    explicit Controller(QObject *parent = nullptr);
    ~Controller() override;

    bool connected() const { return m_client.isConnected() && m_haveState; }
    QString problem() const { return m_problem; }
    QVariantMap state() const { return m_state; }
    QVariantList spectrum() const { return m_spectrumOut; }
    bool spectrumWanted() const { return m_spectrumWanted; }
    void setSpectrumWanted(bool wanted);
    QVariantList searchResults() const { return m_searchResults; }
    bool searchPending() const { return m_searchPending; }
    QString searchError() const { return m_searchError; }
    QVariantList availablePlugins() const { return m_plugins; }
    QVariantList library() const { return m_library; }
    QString notice() const { return m_notice; }

    // Any daemon command, as {"cmd": ..., ...}.
    Q_INVOKABLE void send(const QVariantMap &message);
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void applySearchResult(const QVariantMap &result);
    Q_INVOKABLE void loadPlugins();
    Q_INVOKABLE double response(const QVariantList &bands, double freq) const;
    Q_INVOKABLE double bandResponse(const QVariantMap &band, double freq) const;

signals:
    void connectedChanged();
    void stateChanged();
    void spectrumChanged();
    void spectrumWantedChanged();
    void searchChanged();
    void availablePluginsChanged();
    void libraryChanged();
    void noticeChanged();

private:
    void onMessage(const QJsonObject &message);
    void loadLibrary();
    void subscribe();
    void retry();
    void startService();
    void setNotice(const QString &text);
    void animate();

    ipc::Client m_client;
    QVariantMap m_state;
    bool m_haveState = false;
    QString m_problem;
    QTimer m_retry;
    QElapsedTimer m_down;
    bool m_triedService = false;
    QProcess m_systemctl;

    bool m_spectrumWanted = false;
    QVector<float> m_target, m_levels;
    QVariantList m_spectrumOut;
    QTimer m_frame;

    QString m_query;
    int m_searchId = 0;
    QVariantList m_searchResults;
    bool m_searchPending = false;
    QString m_searchError;
    QVariantList m_plugins;
    QVariantList m_library;
    int m_libraryRevision = -1;
    QString m_notice;
    QTimer m_noticeTimer;
};
