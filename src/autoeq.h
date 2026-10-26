#pragma once

// Headphone profiles from the AutoEq project at github.com/jaakkopasanen/AutoEq.
// The index of every measured headphone is fetched once a week and cached.
// A profile is fetched when someone picks it.

#include "eq.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QVector>
#include <functional>

class AutoEq : public QObject {
    Q_OBJECT

public:
    struct Entry {
        QString name;   // "Sennheiser HD 650"
        QString path;   // "oratory1990/over-ear/Sennheiser HD 650"
        QString source; // "oratory1990"
    };

    explicit AutoEq(const QString &cacheDir, QObject *parent = nullptr);

    bool ready() const { return !m_entries.isEmpty(); }
    QString error() const { return m_error; }
    void ensureIndex();

    // Best entry per headphone, measurements by oratory1990 and crinacle
    // first, matched ignoring case and spaces.
    QVector<Entry> search(const QString &query, int limit) const;

    using Done = std::function<void(const eq::Bands &bands, const QString &error)>;
    void fetch(const QString &path, const QString &name, Done done);

    // "Sennheiser HD 650" -> "HD 650 · AutoEQ"
    static QString presetName(const QString &headphone);
    static QVector<Entry> parseIndex(const QString &markdown);

signals:
    void indexReady();

private:
    void loadIndex(const QByteArray &data);

    QString m_cacheDir;
    QNetworkAccessManager m_network;
    QVector<Entry> m_entries;
    QString m_error;
    bool m_fetching = false;
};
