#include "controller.h"

#include "eq.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace {

constexpr int Bands = 64;
constexpr float Floor = -72;

eq::Band bandFrom(const QVariantMap &m) {
    eq::Band b;
    b.freq = m.value("f").toDouble();
    b.gain = m.value("g").toDouble();
    b.q = m.value("q").toDouble();
    b.type = eq::typeFromName(m.value("t").toString());
    return b;
}

} // namespace

Controller::Controller(QObject *parent) : QObject(parent), m_target(Bands, Floor), m_levels(Bands, Floor) {
    connect(&m_client, &ipc::Client::connected, this, [this] {
        m_problem.clear();
        subscribe();
    });
    connect(&m_client, &ipc::Client::disconnected, this, [this] {
        const bool was = connected();
        m_haveState = false;
        if (!m_down.isValid())
            m_down.start();
        if (was)
            emit connectedChanged();
        m_retry.start();
    });
    connect(&m_client, &ipc::Client::message, this, &Controller::onMessage);

    m_retry.setInterval(500);
    connect(&m_retry, &QTimer::timeout, this, &Controller::retry);

    // The analyzer is drawn at the display's pace and eased between the
    // daemon's frames: quick to rise, slow to fall.
    m_frame.setInterval(16);
    connect(&m_frame, &QTimer::timeout, this, &Controller::animate);

    m_noticeTimer.setSingleShot(true);
    m_noticeTimer.setInterval(4000);
    connect(&m_noticeTimer, &QTimer::timeout, this, [this] { setNotice({}); });

    for (int i = 0; i < Bands; ++i)
        m_spectrumOut << double(Floor);

    m_down.start();
    m_client.connectToDaemon();
    m_retry.start();
}

// The client is the first member, so it is destroyed last. Its close signal
// must not reach the timers that are already gone.
Controller::~Controller() {
    disconnect(&m_client, nullptr, this, nullptr);
    disconnect(&m_systemctl, nullptr, this, nullptr);
}

void Controller::retry() {
    if (m_client.isConnected()) {
        m_retry.stop();
        return;
    }
    // The daemon normally runs from login. When it does not, start it once,
    // and from then on at every login, since it is what makes the curve
    // apply to every app.
    if (!m_triedService && m_down.elapsed() > 600 && !qEnvironmentVariableIsSet("PREAMP_NO_AUTOSTART"))
        startService();
    if (m_down.elapsed() > 6000 && m_problem.isEmpty()) {
        m_problem = tr("The preamp service is not running. Start it with: systemctl --user enable --now preamp.service");
        emit connectedChanged();
    }
    m_client.connectToDaemon();
}

void Controller::startService() {
    m_triedService = true;
    m_systemctl.start("systemctl", {"--user", "enable", "--now", "preamp.service"});
}

void Controller::subscribe() {
    m_client.send({{"cmd", "subscribe"}, {"state", true}, {"spectrum", m_spectrumWanted}});
}

void Controller::setSpectrumWanted(bool wanted) {
    if (wanted == m_spectrumWanted)
        return;
    m_spectrumWanted = wanted;
    if (m_client.isConnected())
        subscribe();
    if (wanted) {
        m_frame.start();
    } else {
        m_frame.stop();
        std::fill(m_levels.begin(), m_levels.end(), Floor);
        std::fill(m_target.begin(), m_target.end(), Floor);
    }
    emit spectrumWantedChanged();
}

void Controller::onMessage(const QJsonObject &message) {
    const QString type = message.value("type").toString();
    if (type == "state") {
        const bool was = connected();
        m_state = message.toVariantMap();
        m_haveState = true;
        m_down.invalidate();
        m_problem.clear();
        if (!was)
            emit connectedChanged();
        emit stateChanged();
        // A daemon that predates the library sends no revision.
        const int revision = message.value("library").toInt(-1);
        if (revision >= 0 && (revision != m_libraryRevision || !was)) {
            m_libraryRevision = revision;
            loadLibrary();
        }
    } else if (type == "spectrum") {
        const QJsonArray l = message.value("l").toArray();
        for (int i = 0; i < Bands && i < l.size(); ++i)
            m_target[i] = std::max(Floor, float(l[i].toDouble()));
    } else if (type == "search") {
        if (message.value("q").toString() != m_query)
            return;
        m_searchResults = message.value("results").toArray().toVariantList();
        m_searchPending = message.value("pending").toBool();
        m_searchError = message.value("error").toString();
        emit searchChanged();
    }
}

void Controller::animate() {
    bool moved = false;
    for (int i = 0; i < Bands; ++i) {
        const float target = m_target[i], level = m_levels[i];
        const float next = target > level ? level + (target - level) * .45f : std::max(target, level - .9f);
        if (std::abs(next - level) > 1e-3f) {
            m_levels[i] = next;
            m_spectrumOut[i] = double(next);
            moved = true;
        }
    }
    if (moved)
        emit spectrumChanged();
}

void Controller::send(const QVariantMap &message) {
    if (!m_client.isConnected())
        return;
    const QString cmd = message.value("cmd").toString();
    m_client.send(QJsonObject::fromVariantMap(message), [this, cmd](const QJsonObject &reply) {
        if (!reply.value("ok").toBool() && cmd != "state")
            setNotice(reply.value("error").toString());
    });
}

void Controller::search(const QString &query) {
    m_query = query;
    if (query.trimmed().isEmpty()) {
        m_searchResults.clear();
        m_searchPending = false;
        m_searchError.clear();
        emit searchChanged();
        return;
    }
    m_client.send({{"cmd", "search"}, {"q", query}, {"id", ++m_searchId}});
}

void Controller::applySearchResult(const QVariantMap &result) {
    if (result.contains("path")) {
        setNotice(tr("Fetching %1 from AutoEq").arg(result.value("t").toString()));
        m_client.send({{"cmd", "autoeq"}, {"path", result.value("path").toString()}}, [this](const QJsonObject &reply) {
            if (reply.value("ok").toBool())
                setNotice({});
            else
                setNotice(tr("AutoEq: %1").arg(reply.value("error").toString()));
        });
        return;
    }
    send({{"cmd", "applyPreset"}, {"name", result.value("n").toString()}});
}

void Controller::loadPlugins() {
    m_client.send({{"cmd", "plugins"}}, [this](const QJsonObject &reply) {
        m_plugins = reply.value("plugins").toArray().toVariantList();
        emit availablePluginsChanged();
    });
}

void Controller::loadLibrary() {
    m_client.send({{"cmd", "library"}}, [this](const QJsonObject &reply) {
        if (!reply.value("ok").toBool())
            return;
        m_library = reply.value("presets").toArray().toVariantList();
        emit libraryChanged();
    });
}

double Controller::response(const QVariantList &bands, double freq) const {
    double sum = 0;
    for (const QVariant &v : bands)
        sum += eq::response(bandFrom(v.toMap()), freq);
    return sum;
}

double Controller::bandResponse(const QVariantMap &band, double freq) const {
    return eq::response(bandFrom(band), freq);
}

void Controller::setNotice(const QString &text) {
    if (text == m_notice)
        return;
    m_notice = text;
    if (!text.isEmpty())
        m_noticeTimer.start();
    emit noticeChanged();
}
