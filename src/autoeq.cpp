#include "autoeq.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

namespace {

const char *BaseUrl = "https://raw.githubusercontent.com/jaakkopasanen/AutoEq/master/results/";
constexpr qint64 MaxIndexBytes = 8 << 20;
constexpr qint64 MaxProfileBytes = 64 << 10;
constexpr int TimeoutMs = 20000;

QString squash(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (const QChar c : s)
        if (!c.isSpace())
            out += c.toLower();
    return out;
}

int sourceRank(const QString &source) {
    if (source == "oratory1990")
        return 0;
    if (source == "crinacle")
        return 1;
    if (source == "Rtings")
        return 2;
    return 3;
}

// Downloads one file over HTTPS with a size cap and a timeout.
void download(QNetworkAccessManager &network, const QUrl &url, qint64 cap,
              std::function<void(const QByteArray &, const QString &)> done) {
    QNetworkRequest request(url);
    request.setTransferTimeout(TimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network.get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply, cap](qint64 received, qint64) {
        if (received > cap)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, cap, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, reply->error() == QNetworkReply::OperationCanceledError ? QStringLiteral("download too large")
                                                                             : reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        if (data.size() > cap) {
            done({}, QStringLiteral("download too large"));
            return;
        }
        done(data, {});
    });
}

} // namespace

AutoEq::AutoEq(const QString &cacheDir, QObject *parent) : QObject(parent), m_cacheDir(cacheDir) {
    QFile cached(m_cacheDir + "/autoeq-index.md");
    if (cached.open(QIODevice::ReadOnly) && cached.size() <= MaxIndexBytes)
        loadIndex(cached.readAll());
}

QVector<AutoEq::Entry> AutoEq::parseIndex(const QString &markdown) {
    // "- [Sennheiser HD 650](./oratory1990/over-ear/Sennheiser%20HD%20650) by oratory1990"
    static const QRegularExpression line(R"(^- \[(.{1,160}?)\]\(\./([^)]{1,400})\))", QRegularExpression::MultilineOption);
    QVector<Entry> entries;
    auto it = line.globalMatch(markdown);
    while (it.hasNext()) {
        const auto m = it.next();
        Entry e;
        e.name = m.captured(1).trimmed();
        e.path = QUrl::fromPercentEncoding(m.captured(2).toUtf8());
        e.source = e.path.section('/', 0, 0);
        // Paths are relative inside the results tree. Anything that steps
        // out of it is not an entry.
        if (e.name.isEmpty() || e.path.contains("..") || e.path.startsWith('/'))
            continue;
        entries << e;
    }
    return entries;
}

void AutoEq::loadIndex(const QByteArray &data) {
    const auto entries = parseIndex(QString::fromUtf8(data));
    if (!entries.isEmpty())
        m_entries = entries;
}

void AutoEq::ensureIndex() {
    const QFileInfo cached(m_cacheDir + "/autoeq-index.md");
    const bool fresh = cached.exists() && cached.lastModified().daysTo(QDateTime::currentDateTime()) < 7;
    if ((ready() && fresh) || m_fetching)
        return;
    m_fetching = true;
    download(m_network, QUrl(QString(BaseUrl) + "INDEX.md"), MaxIndexBytes, [this](const QByteArray &data, const QString &error) {
        m_fetching = false;
        if (!error.isEmpty()) {
            m_error = error;
            emit indexReady();
            return;
        }
        if (parseIndex(QString::fromUtf8(data)).isEmpty()) {
            m_error = QStringLiteral("the AutoEq index changed format");
            emit indexReady();
            return;
        }
        QDir().mkpath(m_cacheDir);
        QSaveFile file(m_cacheDir + "/autoeq-index.md");
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.commit();
        }
        m_error.clear();
        loadIndex(data);
        emit indexReady();
    });
}

QVector<AutoEq::Entry> AutoEq::search(const QString &query, int limit) const {
    const QString q = squash(query);
    if (q.isEmpty())
        return {};
    QHash<QString, Entry> best;
    QStringList order;
    for (const Entry &e : m_entries) {
        if (!squash(e.name).contains(q))
            continue;
        auto it = best.find(e.name);
        if (it == best.end()) {
            best.insert(e.name, e);
            order << e.name;
        } else if (sourceRank(e.source) < sourceRank(it->source)) {
            *it = e;
        }
    }
    // Names that start with the query first, then the shortest, which is
    // usually the base model rather than a variant.
    std::stable_sort(order.begin(), order.end(), [&](const QString &a, const QString &b) {
        const bool pa = squash(a).startsWith(q), pb = squash(b).startsWith(q);
        if (pa != pb)
            return pa;
        return a.size() < b.size();
    });
    QVector<Entry> out;
    for (const QString &name : std::as_const(order)) {
        if (out.size() >= limit)
            break;
        out << best.value(name);
    }
    return out;
}

void AutoEq::fetch(const QString &path, const QString &name, Done done) {
    if (path.contains("..") || path.startsWith('/')) {
        done({}, QStringLiteral("not an AutoEq profile"));
        return;
    }
    const QString file = path.section('/', -1) + " ParametricEQ.txt";
    const QUrl url(QString(BaseUrl) + QUrl::toPercentEncoding(path, "/") + "/" + QUrl::toPercentEncoding(file));
    download(m_network, url, MaxProfileBytes, [done, name](const QByteArray &data, const QString &error) {
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        const eq::Bands bands = eq::parseAutoEq(QString::fromUtf8(data));
        if (bands.isEmpty()) {
            done({}, QStringLiteral("no filters in the %1 profile").arg(name));
            return;
        }
        done(bands, {});
    });
}

QString AutoEq::presetName(const QString &headphone) {
    // Brands written as more than one word, kept whole when they are dropped.
    static const QStringList brands{
        "Audio-Technica", "Bang & Olufsen", "Beyerdynamic", "Sennheiser", "Sony", "Apple", "HiFiMAN", "Moondrop",
        "AKG", "Audeze", "Focal", "Bose", "Shure", "Grado", "Koss", "Philips", "Samsung", "Jabra", "Final",
        "Fiio", "FiiO", "Dan Clark Audio", "ZMF", "Meze", "Stax", "Campfire Audio", "64 Audio", "Etymotic",
        "Google", "Nothing", "Anker", "Soundcore", "JBL", "Marshall", "Skullcandy", "Razer", "SteelSeries",
        "HyperX", "Logitech", "Corsair", "Tin HiFi", "Truthear", "KZ", "Letshuoer", "7Hz", "Thieaudio",
    };
    QString name = headphone.trimmed();
    for (const QString &brand : brands) {
        if (name.startsWith(brand + " ", Qt::CaseInsensitive)) {
            name = name.mid(brand.size() + 1).trimmed();
            break;
        }
    }
    return name + QString::fromUtf8(" · AutoEQ");
}
