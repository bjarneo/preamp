#include "daemon.h"

#include "brand.h"
#include "plugins.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {

const QStringList RoomNames{"off", "small room", "studio", "hall"};

dsp::Room roomFromName(const QString &name) {
    const int i = RoomNames.indexOf(name);
    return i < 0 ? dsp::Room::Off : dsp::Room(i);
}

QString roomName(dsp::Room room) { return RoomNames.value(int(room)); }

QString squash(const QString &s) {
    QString out;
    for (const QChar c : s)
        if (!c.isSpace())
            out += c.toLower();
    return out;
}

// The window's gain format: a real minus, a plus-minus at zero.
QString formatGain(double g) {
    const QString sign = g > 0.04 ? QStringLiteral("+") : g < -0.04 ? QStringLiteral("−") : QStringLiteral("±");
    return sign + QString::number(std::abs(g), 'f', 1);
}

QString formatRate(int rate) {
    if (rate <= 0)
        rate = 48000;
    if (rate % 1000 == 0)
        return QStringLiteral("%1 kHz").arg(rate / 1000);
    return QStringLiteral("%1 kHz").arg(rate / 1000.0, 0, 'f', 1);
}

// Plugins call themselves "… Stereo"; in a stereo chain that says nothing.
QString pluginLabel(const QString &name) {
    QString out = name;
    for (const char *suffix : {" Stereo", " stereo", " x2"})
        if (out.endsWith(QLatin1String(suffix)))
            out.chop(int(qstrlen(suffix)));
    return out;
}

// "PipeWire ALSA [cliamp]" is cliamp.
QString appLabel(const QString &name) {
    static const QRegularExpression alsa(R"(^PipeWire ALSA \[(.+)\]$)");
    const auto m = alsa.match(name);
    return m.hasMatch() ? m.captured(1) : name;
}

int volumePercent(const PwEngine::Node &n) {
    if (!n.hasVolume || n.volumes.isEmpty())
        return 100;
    const float loudest = *std::max_element(n.volumes.begin(), n.volumes.end());
    return int(std::lround(std::cbrt(std::max(0.f, loudest)) * 100));
}

} // namespace

Daemon::Daemon(const QString &configDir, const QString &cacheDir, QObject *parent)
    : QObject(parent), m_configDir(configDir), m_cacheDir(cacheDir), m_engine(&m_processor), m_autoeq(cacheDir) {
    m_left = m_right = eq::flat();

    m_stateTimer.setSingleShot(true);
    m_stateTimer.setInterval(16);
    connect(&m_stateTimer, &QTimer::timeout, this, &Daemon::broadcastState);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &Daemon::save);
    m_spectrumTimer.setInterval(33);
    connect(&m_spectrumTimer, &QTimer::timeout, this, &Daemon::tickSpectrum);
    m_collectTimer.setInterval(250);
    connect(&m_collectTimer, &QTimer::timeout, this, [this] { m_processor.collect(); });
}

Daemon::~Daemon() = default;

QStringList Daemon::rooms() { return RoomNames; }

bool Daemon::start(QString *error) {
    load();
    if (!m_server.listen(error))
        return false;
    connect(&m_server, &ipc::Server::message, this, &Daemon::onMessage);
    connect(&m_server, &ipc::Server::subscribersChanged, this, [this] {
        bool any = false;
        for (ipc::Connection *c : m_server.connections())
            any |= c->wantsSpectrum;
        if (any && !m_spectrumTimer.isActive())
            m_spectrumTimer.start();
        else if (!any)
            m_spectrumTimer.stop();
    });

    connect(&m_engine, &PwEngine::graphChanged, this, &Daemon::onGraph);
    connect(&m_engine, &PwEngine::defaultsChanged, this, &Daemon::onDefaults);
    connect(&m_engine, &PwEngine::rateChanged, this, [this] {
        pushDsp();
        rebuildChain();
        queueState();
    });
    connect(&m_engine, &PwEngine::disconnected, this, [] {
        qWarning("preamp: lost the connection to PipeWire");
        QCoreApplication::exit(1);
    });
    if (!m_engine.start(error))
        return false;

    pushDsp();
    rebuildChain();
    m_collectTimer.start();

    connect(&m_hyprland, &Hyprland::changed, this, &Daemon::evaluateRules);
    connect(&m_autoeq, &AutoEq::indexReady, this, [this] {
        const auto pending = std::exchange(m_pendingSearches, {});
        for (const auto &p : pending)
            if (p.first)
                search(p.second.first, p.first, p.second.second);
    });
    m_theme.stopRoundingPoll();
    connect(&m_theme, &Theme::changed, this, &Daemon::refreshIcon);
    refreshIcon();

    // Two round trips: the first lists every object, the second brings the
    // details of the ones bound in between.
    m_engine.sync([this] { m_engine.sync([this] { routeFirstTime(); }); });
    return true;
}

void Daemon::stop() {
    m_stopping = true;
    if (m_routed && !m_output.isEmpty() && m_engine.isConnected() && !qEnvironmentVariableIsSet("PREAMP_NO_DEFAULT")) {
        m_engine.setConfiguredDefaultSink(m_output);
        QEventLoop loop;
        m_engine.sync([&loop] { loop.quit(); });
        QTimer::singleShot(800, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (m_saveTimer.isActive())
        save();
}

// ------------------------------------------------------------------ model

void Daemon::load() {
    QFile file(m_configDir + "/config.json");
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    m_seenFirstRun = true;

    const QJsonObject bands = o.value("bands").toObject();
    m_left = eq::fromJson(bands.value("L").toArray());
    m_right = bands.contains("R") ? eq::fromJson(bands.value("R").toArray()) : m_left;
    m_link = o.value("link").toBool(true);
    if (m_link)
        m_right = m_left;
    m_channel = o.value("channel").toString("L") == "R" ? "R" : "L";
    m_preset = o.value("preset").toString("Flat");
    m_dirty = o.value("dirty").toBool(false);
    m_bypass = o.value("bypass").toBool(false);

    for (const auto &v : o.value("presets").toArray()) {
        const QJsonObject p = v.toObject();
        const QString name = p.value("name").toString().left(120);
        if (name.isEmpty())
            continue;
        Preset preset{name, p.value("kind").toString("user"), eq::fromJson(p.value("L").toArray()), {}};
        preset.right = p.contains("R") ? eq::fromJson(p.value("R").toArray()) : preset.left;
        m_presets << preset;
    }
    for (const auto &v : o.value("recent").toArray())
        m_recent << v.toString();
    const QJsonObject pins = o.value("pins").toObject();
    for (auto it = pins.begin(); it != pins.end(); ++it)
        m_pins.insert(it.key(), it.value().toString());
    const QJsonObject labels = o.value("outputLabels").toObject();
    for (auto it = labels.begin(); it != labels.end(); ++it)
        m_outputLabels.insert(it.key(), it.value().toString());
    for (const auto &v : o.value("history").toArray())
        m_history << v.toString();
    for (const auto &v : o.value("rules").toArray()) {
        const QJsonObject r = v.toObject();
        Rule rule{r.value("kind").toString(), r.value("when").toString(), r.value("then").toString(), r.value("room").toString()};
        if ((rule.kind == "call" || rule.kind == "fullscreen") && !rule.when.isEmpty())
            m_rules << rule;
    }
    for (const auto &v : o.value("plugins").toArray()) {
        const QJsonObject p = v.toObject();
        Plugin plugin{p.value("id").toString(), p.value("name").toString(), p.value("kind").toString(), p.value("on").toBool(), QString()};
        const QJsonObject params = p.value("params").toObject();
        for (auto it = params.begin(); it != params.end(); ++it)
            plugin.values.insert(it.key(), float(it.value().toDouble()));
        if (!plugin.id.isEmpty())
            m_plugins << plugin;
    }
    m_room = roomFromName(o.value("room").toString("off"));
    m_width = std::clamp(o.value("width").toDouble(100), 0.0, 200.0);
    m_balance = std::clamp(o.value("balance").toDouble(0), -100.0, 100.0);
    m_output = o.value("output").toString();
}

void Daemon::scheduleSave() { m_saveTimer.start(); }

void Daemon::save() {
    m_saveTimer.stop();
    QJsonObject o;
    o.insert("version", 1);
    o.insert("bands", QJsonObject{{"L", eq::toJson(m_left)}, {"R", eq::toJson(m_link ? m_left : m_right)}});
    o.insert("link", m_link);
    o.insert("channel", m_channel);
    o.insert("preset", m_preset);
    o.insert("dirty", m_dirty);
    o.insert("bypass", m_bypass);
    QJsonArray presets;
    for (const Preset &p : std::as_const(m_presets))
        presets.append(QJsonObject{{"name", p.name}, {"kind", p.kind}, {"L", eq::toJson(p.left)}, {"R", eq::toJson(p.right)}});
    o.insert("presets", presets);
    o.insert("recent", QJsonArray::fromStringList(m_recent));
    QJsonObject pins, labels;
    for (auto it = m_pins.begin(); it != m_pins.end(); ++it)
        pins.insert(it.key(), it.value());
    for (auto it = m_outputLabels.begin(); it != m_outputLabels.end(); ++it)
        labels.insert(it.key(), it.value());
    o.insert("pins", pins);
    o.insert("outputLabels", labels);
    o.insert("history", QJsonArray::fromStringList(m_history));
    QJsonArray rules;
    for (const Rule &r : std::as_const(m_rules))
        rules.append(QJsonObject{{"kind", r.kind}, {"when", r.when}, {"then", r.then}, {"room", r.room}});
    o.insert("rules", rules);
    QJsonArray plugins;
    for (const Plugin &p : std::as_const(m_plugins)) {
        QJsonObject plugin{{"id", p.id}, {"name", p.name}, {"kind", p.kind}, {"on", p.on}};
        if (!p.values.isEmpty()) {
            QJsonObject params;
            for (auto it = p.values.begin(); it != p.values.end(); ++it)
                params.insert(it.key(), it.value());
            plugin.insert("params", params);
        }
        plugins.append(plugin);
    }
    o.insert("plugins", plugins);
    o.insert("room", roomName(m_room));
    o.insert("width", m_width);
    o.insert("balance", m_balance);
    o.insert("output", m_output);

    QDir().mkpath(m_configDir);
    QSaveFile file(m_configDir + "/config.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        if (!file.commit())
            qWarning("preamp: could not save %s", qPrintable(file.fileName()));
    }
}

void Daemon::changed(bool dsp) {
    if (dsp)
        pushDsp();
    scheduleSave();
    queueState();
}

void Daemon::pushDsp() {
    const double rate = m_engine.sampleRate() > 0 ? m_engine.sampleRate() : 48000;
    const eq::Bands &right = m_link ? m_left : m_right;
    m_processor.post(dsp::makeParams(rate, !m_bypass, m_left, right, eq::autoPreamp(m_left, right), m_width / 100, m_room,
                                     m_balance));
}

void Daemon::rebuildChain() {
    for (Plugin &p : m_plugins)
        p.live = nullptr;
    const double rate = m_engine.sampleRate();
    if (rate <= 0)
        return;
    auto chain = std::make_unique<dsp::Chain>();
    for (Plugin &p : m_plugins) {
        p.error.clear();
        if (!p.on)
            continue;
        QString error;
        auto instance = plugins::Catalog::shared().instantiate(p.id, rate, p.values, &error);
        if (instance) {
            p.live = instance.get();
            chain->instances.push_back(std::move(instance));
        } else
            p.error = error.isEmpty() ? QStringLiteral("failed") : error;
    }
    m_processor.post(std::move(chain));
}

eq::Bands &Daemon::editing() {
    return (m_link || m_channel == "L") ? m_left : m_right;
}

void Daemon::setBands(const std::function<eq::Bands(eq::Bands)> &edit) {
    if (m_link) {
        m_left = eq::normalized(edit(m_left));
        m_right = m_left;
    } else {
        editing() = eq::normalized(edit(editing()));
    }
    m_dirty = true;
    changed();
}

QVector<Daemon::Preset> Daemon::library() const {
    QVector<Preset> all;
    for (const eq::Preset &p : eq::builtins())
        all << Preset{p.name, p.kind, p.left, p.right};
    for (const Preset &p : m_presets)
        all << p;
    return all;
}

const Daemon::Preset *Daemon::findPreset(const QString &name) const {
    static thread_local QVector<Preset> builtins;
    if (builtins.isEmpty())
        for (const eq::Preset &p : eq::builtins())
            builtins << Preset{p.name, p.kind, p.left, p.right};
    for (const Preset &p : builtins)
        if (p.name == name)
            return &p;
    for (const Preset &p : m_presets)
        if (p.name == name)
            return &p;
    return nullptr;
}

// Keys 1 to 8: the everyday presets, then the most recent other presets,
// headphone profiles and saved presets.
QStringList Daemon::presetSlots() const {
    QStringList out;
    for (const eq::Preset &p : eq::builtins().mid(0, eq::EverydayPresets))
        out << p.name;
    for (const QString &name : m_recent) {
        if (out.size() >= 8)
            break;
        if (findPreset(name) && !out.contains(name))
            out << name;
    }
    return out;
}

void Daemon::remember(const QString &name) {
    for (const eq::Preset &p : eq::builtins().mid(0, eq::EverydayPresets))
        if (p.name == name)
            return;
    m_recent.removeAll(name);
    m_recent.prepend(name);
    while (m_recent.size() > 8)
        m_recent.removeLast();
}

bool Daemon::applyPreset(const QString &name, bool fromRule) {
    const Preset *p = findPreset(name);
    if (!p)
        return false;
    m_left = p->left;
    m_right = p->right;
    m_link = p->left == p->right;
    m_preset = p->name;
    m_dirty = false;
    remember(p->name);
    if (!fromRule && m_beforeRule) {
        // Picked by hand while a rule holds: that choice is what comes back
        // when the rule lets go.
        m_beforeRule->left = m_left;
        m_beforeRule->right = m_right;
        m_beforeRule->preset = m_preset;
        m_beforeRule->dirty = false;
    }
    changed();
    return true;
}

// ------------------------------------------------------------------ graph

QVector<Daemon::Output> Daemon::outputs() const {
    const auto &nodes = m_engine.nodes();

    // A DSP sink in front of a device, like Omarchy's speaker tuning, plays
    // into it through a stream named after itself. Pick the DSP sink and hide
    // the device: playing straight into it would skip the tuning. A sink in
    // front of another DSP sink hides nothing, or a loopback that plays into
    // the tuning would hide the tuning and take preamp's output.
    QHash<uint32_t, uint32_t> fronts; // virtual sink -> the sink it plays into
    QSet<uint32_t> fronted;
    for (const PwEngine::Node &v : nodes) {
        if (!v.isSink() || v.deviceId != 0 || v.name == PwEngine::sinkName() || v.name.isEmpty())
            continue;
        QString outName = v.name;
        if (outName.startsWith("effect_input."))
            outName.replace(0, 13, "effect_output.");
        for (const PwEngine::Node &s : nodes) {
            if (!s.isPlayback() || s.name == PwEngine::outputName())
                continue;
            if (s.name != outName && !s.name.startsWith(v.name))
                continue;
            const uint32_t target = m_engine.linkedSinkOf(s.id);
            if (target && target != v.id) {
                fronts.insert(v.id, target);
                if (nodes.value(target).deviceId)
                    fronted.insert(target);
            }
        }
    }

    // Loudness lives on the device at the end of the chain, through every
    // DSP sink in front of it.
    auto deviceOf = [&](const PwEngine::Node &n) {
        PwEngine::Node device = n;
        QSet<uint32_t> seen{n.id};
        while (fronts.contains(device.id) && !seen.contains(fronts.value(device.id))) {
            device = nodes.value(fronts.value(device.id));
            seen.insert(device.id);
        }
        return device;
    };

    auto kindOf = [&](const PwEngine::Node &n) -> QString {
        const PwEngine::Device d = m_engine.devices().value(n.deviceId);
        if (n.deviceApi == "bluez5" || d.bus == "bluetooth" || d.api == "bluez5")
            return "bluetooth";
        const QString text = (n.name + " " + n.description).toLower();
        if (text.contains("hdmi") || text.contains("displayport"))
            return "hdmi";
        if (!d.bus.isEmpty())
            return d.bus;
        return n.deviceId ? QStringLiteral("pci") : QStringLiteral("virtual");
    };
    auto labelOf = [&](const PwEngine::Node &n, const QString &kind) -> QString {
        const PwEngine::Device d = m_engine.devices().value(n.deviceId);
        if (kind == "bluetooth")
            return !d.description.isEmpty() ? d.description : n.description;
        if (kind == "hdmi")
            return !n.nick.isEmpty() ? n.nick : n.description;
        if (kind == "usb" && !d.description.isEmpty())
            return d.description;
        if (n.nick == "Speaker")
            return QStringLiteral("Built-in speakers");
        if (n.nick == "Headphones")
            return QStringLiteral("Headphones");
        return !n.description.isEmpty() ? n.description : n.name;
    };

    QVector<Output> list;
    for (const PwEngine::Node &n : nodes) {
        if (!n.isSink() || n.name == PwEngine::sinkName() || n.name.isEmpty() || fronted.contains(n.id))
            continue;
        if (!m_engine.isAvailable(n) && n.name != m_output)
            continue;
        const PwEngine::Node device = deviceOf(n);
        Output o;
        o.name = n.name;
        o.id = n.id;
        o.hardware = n.deviceId != 0;
        o.volumeNode = device.id;
        o.kind = kindOf(device);
        o.label = fronts.contains(n.id) && !n.description.isEmpty() ? n.description : labelOf(device, o.kind);
        o.rate = formatRate(device.rate ? device.rate : n.rate ? n.rate : int(m_engine.sampleRate()));
        o.priority = std::max(n.priority, device.priority);
        list << o;
    }
    std::stable_sort(list.begin(), list.end(), [](const Output &a, const Output &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.id < b.id;
    });
    return list;
}

const Daemon::Output *Daemon::currentOutput(const QVector<Output> &list) const {
    for (const Output &o : list)
        if (o.name == m_output)
            return &o;
    return nullptr;
}

QString Daemon::desiredOutput(const QVector<Output> &list) const {
    for (const QString &name : m_history)
        for (const Output &o : list)
            if (o.name == name)
                return name;
    return list.isEmpty() ? QString() : list.first().name;
}

QVector<Daemon::App> Daemon::apps() const {
    const auto &nodes = m_engine.nodes();
    QList<uint32_t> ids = nodes.keys();
    std::sort(ids.begin(), ids.end());

    QVector<App> list;
    auto find = [&](const QString &key) -> App * {
        for (App &a : list)
            if (a.key == key)
                return &a;
        return nullptr;
    };
    for (uint32_t id : ids) {
        const PwEngine::Node &n = nodes[id];
        if (!n.isPlayback() || n.appName.isEmpty() || n.name.startsWith(PwEngine::sinkName()))
            continue;
        App *a = find(n.appName);
        if (!a) {
            App app;
            app.key = n.appName;
            app.name = appLabel(n.appName);
            app.volume = volumePercent(n);
            app.muted = n.mute;
            app.state = "idle";
            list << app;
            a = &list.last();
        }
        a->streams << id;
        if (n.state == "running")
            a->state = "playing";
        else if (n.state == "idle" && a->state != "playing")
            a->state = "paused";
        if (n.mediaRole == "Communication" || n.mediaRole == "phone")
            a->inCall = true;
    }
    // In a call: the same app is also recording.
    for (const PwEngine::Node &n : nodes) {
        if (!n.isCapture() || n.state != "running" || n.appName.isEmpty())
            continue;
        if (App *a = find(n.appName))
            a->inCall = true;
    }
    for (App &a : list)
        if (a.inCall)
            a.state = "in call";
    return list;
}

void Daemon::routeFirstTime() {
    if (m_routed || m_stopping)
        return;
    m_routed = true;
    const QVector<Output> list = outputs();
    auto has = [&](const QString &name) {
        return std::any_of(list.begin(), list.end(), [&](const Output &o) { return o.name == name; });
    };
    const QString configured = m_engine.configuredSink();
    QString pick;
    if (has(configured))
        pick = configured; // where the system was playing before preamp
    else if (has(m_output))
        pick = m_output;
    else
        pick = desiredOutput(list);
    if (m_history.isEmpty() && !pick.isEmpty())
        m_history << pick;
    for (const Output &o : list)
        m_knownSinks.insert(o.name);

    // PREAMP_OUTPUT and PREAMP_NO_DEFAULT run a daemon that leaves the
    // system output alone, for testing beside the real one.
    if (qEnvironmentVariableIsSet("PREAMP_OUTPUT"))
        pick = qEnvironmentVariable("PREAMP_OUTPUT");
    chooseOutput(pick, false);
    if (!qEnvironmentVariableIsSet("PREAMP_NO_DEFAULT")) {
        m_engine.setConfiguredDefaultSink(PwEngine::sinkName());
        reclaimStreams({});
    }

    if (!m_seenFirstRun) {
        // A compressor and a limiter, when LSP is installed, ready to switch on.
        for (const char *uri : {"http://lsp-plug.in/plugins/lv2/compressor_stereo", "http://lsp-plug.in/plugins/lv2/limiter_stereo"}) {
            plugins::Info info;
            if (plugins::Catalog::shared().find(QStringLiteral("lv2:") + uri, &info))
                m_plugins << Plugin{info.id, pluginLabel(info.name), info.kind, false, QString()};
        }
        m_seenFirstRun = true;
        scheduleSave();
    }
    evaluateRules();
    queueState();
}

void Daemon::chooseOutput(const QString &name, bool byUser) {
    if (name.isEmpty())
        return;
    const bool moved = name != m_output;
    m_output = name;
    if (byUser) {
        m_history.removeAll(name);
        m_history.prepend(name);
        while (m_history.size() > 12)
            m_history.removeLast();
    }
    for (const Output &o : outputs())
        if (o.name == name)
            m_outputLabels.insert(name, o.label);
    m_engine.setOutputTarget(name);
    if (moved) {
        evaluateRules();
        changed(false);
    }
}

// Apps that were moved straight to a device, by a switcher that does not
// know about preamp, come back through it.
void Daemon::reclaimStreams(const QString &from) {
    Q_UNUSED(from);
    const QVector<Output> list = outputs();
    QSet<QString> outputNames;
    for (const Output &o : list)
        outputNames.insert(o.name);
    for (const PwEngine::Node &n : m_engine.nodes()) {
        if (!n.isPlayback() || n.appName.isEmpty() || n.name.startsWith(PwEngine::sinkName()))
            continue;
        const QString target = m_engine.targetOf(n.id);
        if (target.isEmpty())
            continue;
        bool toDevice = outputNames.contains(target);
        for (const Output &o : list)
            toDevice |= target == QString::number(o.id) || target == QString::number(m_engine.nodes().value(o.id).serial);
        if (toDevice)
            m_engine.clearTarget(n.id);
    }
}

void Daemon::onDefaults() {
    if (!m_routed || m_stopping || qEnvironmentVariableIsSet("PREAMP_NO_DEFAULT"))
        return;
    const QString configured = m_engine.configuredSink();
    if (configured.isEmpty() || configured == PwEngine::sinkName())
        return;
    const QVector<Output> list = outputs();
    const bool candidate = std::any_of(list.begin(), list.end(), [&](const Output &o) { return o.name == configured; });
    if (!candidate)
        return;
    // Someone picked an output outside preamp, like the Omarchy audio panel
    // or the output switch key. Play into it, and stay the system output.
    chooseOutput(configured, true);
    m_engine.setConfiguredDefaultSink(PwEngine::sinkName());
    // The switcher moves app streams in the same breath; give it a moment.
    QTimer::singleShot(400, this, [this, configured] { reclaimStreams(configured); });
}

void Daemon::onGraph() {
    if (!m_routed || m_stopping) {
        queueState();
        return;
    }
    const QVector<Output> list = outputs();
    QSet<QString> now;
    QString arrived;
    int arrivedPriority = INT_MIN;
    for (const Output &o : list) {
        now.insert(o.name);
        // Only a device is plugged in. A virtual sink, like a loopback or
        // another app's equalizer, appears without anyone picking it.
        if (!m_knownSinks.contains(o.name) && o.hardware && o.priority > arrivedPriority) {
            arrived = o.name;
            arrivedPriority = o.priority;
        }
    }
    m_knownSinks = now;

    const Output *current = currentOutput(list);
    QString next;
    bool connected = false;
    if (!current) {
        // The output went away: fall back the way the session manager would.
        next = desiredOutput(list);
    } else if (!arrived.isEmpty()) {
        // Something was plugged in. Take it when it is one the user picked
        // before this one, or ranks above it.
        const int arrivedRank = m_history.indexOf(arrived), currentRank = m_history.indexOf(m_output);
        const bool remembered = arrivedRank >= 0 && (currentRank < 0 || arrivedRank < currentRank);
        if (remembered || (arrivedRank < 0 && arrivedPriority > current->priority)) {
            next = arrived;
            connected = true;
        }
    }
    if (!next.isEmpty() && next != m_output) {
        const QString before = m_preset;
        chooseOutput(next, connected);
        QString label;
        for (const Output &o : list)
            if (o.name == next)
                label = o.label;
        const QString pre = formatGain(eq::autoPreamp(m_left, m_link ? m_left : m_right)) + " dB";
        const QString summary = connected ? tr("%1 connected").arg(label) : label;
        const QString body = m_preset != before || m_pins.contains(next)
            ? tr("Switched to %1. Preamp %2.").arg(m_preset, pre)
            : tr("Playing through %1. Preamp %2.").arg(label, pre);
        notify(summary, body);
    }
    evaluateRules();
    queueState();
}

// ------------------------------------------------------------------ rules

void Daemon::evaluateRules() {
    if (!m_routed)
        return;
    QString winner, preset, room;
    const QVector<App> running = apps();
    for (int i = 0; i < m_rules.size() && winner.isEmpty(); ++i) {
        const Rule &r = m_rules[i];
        bool hit = false;
        if (r.kind == "call") {
            for (const App &a : running)
                hit |= a.inCall && (a.name.contains(r.when, Qt::CaseInsensitive) || a.key.contains(r.when, Qt::CaseInsensitive));
        } else if (r.kind == "fullscreen") {
            hit = m_hyprland.fullscreen() && (m_hyprland.activeClass().contains(r.when, Qt::CaseInsensitive) ||
                                              m_hyprland.activeTitle().contains(r.when, Qt::CaseInsensitive));
        }
        if (hit) {
            winner = QStringLiteral("rule:%1").arg(i);
            preset = r.then;
            room = r.room;
        }
    }
    if (winner.isEmpty() && m_pins.contains(m_output)) {
        winner = "pin:" + m_output;
        preset = m_pins.value(m_output);
    }
    if (winner == m_activeRule)
        return;
    const QString previous = std::exchange(m_activeRule, winner);

    if (winner.startsWith("rule:")) {
        if (!m_beforeRule)
            m_beforeRule = Snapshot{m_left, m_right, m_preset, m_dirty, m_room};
        applyPreset(preset, true);
        if (!room.isEmpty())
            m_room = roomFromName(room);
    } else if (previous.startsWith("rule:") && m_beforeRule) {
        const Snapshot s = *std::exchange(m_beforeRule, std::nullopt);
        if (winner.startsWith("pin:") && s.preset != preset) {
            m_room = s.room;
            applyPreset(preset, true);
        } else {
            m_left = s.left;
            m_right = s.right;
            m_link = s.left == s.right;
            m_preset = s.preset;
            m_dirty = s.dirty;
            m_room = s.room;
        }
    } else if (winner.startsWith("pin:")) {
        applyPreset(preset, true);
    }
    changed();
}

QJsonArray Daemon::rulesJson() const {
    QJsonArray out;
    const QVector<Output> list = outputs();
    QStringList pinned = m_pins.keys();
    // Outputs that are here first, in the order they are listed, then the
    // ones remembered from before.
    std::stable_sort(pinned.begin(), pinned.end(), [&](const QString &a, const QString &b) {
        auto rank = [&](const QString &n) {
            for (int i = 0; i < list.size(); ++i)
                if (list[i].name == n)
                    return i;
            return int(list.size()) + std::max(0, int(m_history.indexOf(n)));
        };
        return rank(a) < rank(b);
    });
    for (const QString &name : pinned) {
        QString label = m_outputLabels.value(name, name);
        for (const Output &o : list)
            if (o.name == name)
                label = o.label;
        out.append(QJsonObject{{"kind", "output"}, {"key", name}, {"when", label}, {"then", m_pins.value(name)},
                               {"room", ""}, {"active", m_activeRule == "pin:" + name}});
    }
    for (int i = 0; i < m_rules.size(); ++i) {
        const Rule &r = m_rules[i];
        out.append(QJsonObject{{"kind", r.kind}, {"index", i}, {"when", r.when}, {"then", r.then}, {"room", r.room},
                               {"active", m_activeRule == QStringLiteral("rule:%1").arg(i)}});
    }
    return out;
}

// ---------------------------------------------------------------- clients

QJsonObject Daemon::state() const {
    const QVector<Output> list = outputs();
    const Output *current = currentOutput(list);
    const eq::Bands &right = m_link ? m_left : m_right;
    const eq::Bands &edit = (m_link || m_channel == "L") ? m_left : m_right;

    QJsonArray outs;
    for (const Output &o : list)
        outs.append(QJsonObject{{"name", o.name}, {"label", o.label}, {"kind", o.kind}, {"rate", o.rate},
                                {"pin", m_pins.value(o.name)}, {"id", int(o.id)}});
    QJsonArray presets;
    for (const Preset &p : library())
        presets.append(QJsonObject{{"name", p.name}, {"kind", p.kind}});
    QJsonArray appList;
    for (const App &a : apps())
        appList.append(QJsonObject{{"key", a.key}, {"name", a.name}, {"volume", a.volume}, {"muted", a.muted}, {"state", a.state}});
    QJsonArray pluginList;
    for (const Plugin &p : m_plugins)
        pluginList.append(QJsonObject{{"id", p.id}, {"name", p.name}, {"kind", p.kind}, {"on", p.on}, {"error", p.error}});

    int volume = 100;
    if (current)
        volume = volumePercent(m_engine.nodes().value(current->volumeNode));
    const QString pin = m_pins.value(m_output);

    return QJsonObject{
        {"type", "state"},
        {"ready", m_routed && m_engine.isConnected()},
        {"preset", m_preset},
        {"dirty", m_dirty},
        {"bypass", m_bypass},
        {"link", m_link},
        {"channel", m_channel},
        {"bands", QJsonObject{{"L", eq::toJson(m_left)}, {"R", eq::toJson(right)}}},
        {"preamp", eq::autoPreamp(m_left, right)},
        {"macros", QJsonArray{eq::macro(edit, 0), eq::macro(edit, 1), eq::macro(edit, 2)}},
        {"slots", QJsonArray::fromStringList(presetSlots())},
        {"presets", presets},
        {"library", m_libraryRevision},
        {"outputs", outs},
        {"output", m_output},
        {"outputLabel", current ? current->label : m_outputLabels.value(m_output)},
        {"volume", volume},
        {"balance", m_balance},
        {"width", m_width},
        {"room", roomName(m_room)},
        {"rooms", QJsonArray::fromStringList(RoomNames)},
        {"apps", appList},
        {"plugins", pluginList},
        {"rules", rulesJson()},
        {"pin", pin},
        {"auto", !pin.isEmpty() && m_activeRule == "pin:" + m_output && m_preset == pin && !m_dirty},
        {"rate", m_engine.sampleRate()},
    };
}

void Daemon::queueState() {
    if (!m_stateTimer.isActive())
        m_stateTimer.start();
}

void Daemon::broadcastState() {
    const QJsonObject s = state();
    for (ipc::Connection *c : m_server.connections())
        if (c->wantsState)
            c->send(s);
}

void Daemon::tickSpectrum() {
    const QVector<float> levels = m_analyzer.compute(m_processor.ring(), m_engine.sampleRate());
    QJsonArray l;
    for (float v : levels)
        l.append(std::round(v * 10) / 10);
    const QJsonObject message{{"type", "spectrum"}, {"l", l}};
    for (ipc::Connection *c : m_server.connections())
        if (c->wantsSpectrum)
            c->send(message);
}

void Daemon::notify(const QString &summary, const QString &body) {
    QDBusMessage call = QDBusMessage::createMethodCall("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                                       "org.freedesktop.Notifications", "Notify");
    call << QStringLiteral("preamp") << uint(0) << QStringLiteral("preamp") << summary << body << QStringList()
         << QVariantMap{{"desktop-entry", QStringLiteral("preamp")}} << int(4000);
    QDBusConnection::sessionBus().asyncCall(call);
}

void Daemon::refreshIcon() {
    brand::writeThemedIcon(m_theme.bd(), m_theme.ln(), m_theme.ac(), m_theme.radius() > 0);
}

void Daemon::search(const QString &query, ipc::Connection *from, int id) {
    const QString q = squash(query);
    QJsonArray results;
    if (!q.isEmpty()) {
        for (const Preset &p : library())
            if (results.size() < 5 && squash(p.name).contains(q))
                results.append(QJsonObject{{"t", p.name}, {"s", p.kind == "autoeq" ? "autoeq" : "preset"}, {"n", p.name}});
        if (results.size() < 5) {
            for (const AutoEq::Entry &e : m_autoeq.search(query, 5 - int(results.size()))) {
                const QString name = AutoEq::presetName(e.name);
                bool dup = false;
                for (const auto &r : std::as_const(results))
                    dup |= r.toObject().value("n").toString() == name;
                if (!dup)
                    results.append(QJsonObject{{"t", e.name}, {"s", "autoeq"}, {"n", name}, {"path", e.path}});
            }
        }
    }
    const bool pending = !q.isEmpty() && !m_autoeq.ready() && m_autoeq.error().isEmpty();
    if (pending) {
        m_pendingSearches.append({QPointer<ipc::Connection>(from), {query, id}});
        m_autoeq.ensureIndex();
    } else if (!q.isEmpty()) {
        m_autoeq.ensureIndex();
    }
    from->send(QJsonObject{{"type", "search"}, {"q", query}, {"id", id}, {"results", results}, {"pending", pending},
                           {"error", m_autoeq.ready() ? QString() : m_autoeq.error()}});
}

void Daemon::onMessage(ipc::Connection *from, const QJsonObject &message) {
    const QString cmd = message.value("cmd").toString();
    const QJsonValue id = message.value("id");
    if (cmd == "subscribe") {
        from->wantsState = message.value("state").toBool(true);
        from->wantsSpectrum = message.value("spectrum").toBool(false);
        if (from->wantsState)
            from->send(state());
        emit m_server.subscribersChanged();
        return;
    }
    if (cmd == "search") {
        search(message.value("q").toString().left(100), from, id.toInt());
        return;
    }
    if (cmd == "autoeq") {
        const QString path = message.value("path").toString();
        const QString headphone = path.section('/', -1);
        const QString name = AutoEq::presetName(headphone);
        QPointer<ipc::Connection> conn(from);
        m_autoeq.fetch(path, headphone, [this, conn, id, name](const eq::Bands &bands, const QString &error) {
            QJsonObject reply{{"type", "reply"}, {"id", id}, {"ok", error.isEmpty()}};
            if (error.isEmpty()) {
                m_presets.erase(std::remove_if(m_presets.begin(), m_presets.end(), [&](const Preset &p) { return p.name == name; }),
                                m_presets.end());
                m_presets << Preset{name, "autoeq", bands, bands};
                ++m_libraryRevision;
                applyPreset(name);
                reply.insert("preset", name);
            } else {
                reply.insert("error", error);
            }
            if (conn && !id.isUndefined())
                conn->send(reply);
        });
        return;
    }
    QJsonObject reply = handle(message);
    if (!id.isUndefined()) {
        reply.insert("type", "reply");
        reply.insert("id", id);
        from->send(reply);
    }
}

QJsonObject Daemon::handle(const QJsonObject &m) {
    const QString cmd = m.value("cmd").toString();
    auto ok = [](QJsonObject extra = {}) {
        extra.insert("ok", true);
        return extra;
    };
    auto fail = [](const QString &error) { return QJsonObject{{"ok", false}, {"error", error}}; };
    auto bandIndex = [&]() { return std::clamp(m.value("i").toInt(), 0, eq::BandCount - 1); };

    if (cmd == "state")
        return ok({{"state", state()}});

    if (cmd == "setBand") {
        const int i = bandIndex();
        setBands([&](eq::Bands b) {
            if (m.contains("f"))
                b[i].freq = eq::clampFreq(m.value("f").toDouble());
            if (m.contains("g"))
                b[i].gain = eq::clampGain(m.value("g").toDouble());
            if (m.contains("q"))
                b[i].q = eq::clampQ(m.value("q").toDouble());
            if (m.contains("t"))
                b[i].type = eq::typeFromName(m.value("t").toString());
            return b;
        });
        return ok();
    }
    if (cmd == "setMacro") {
        const int k = std::clamp(m.value("k").toInt(), 0, 2);
        const double v = eq::clampGain(m.value("v").toDouble());
        setBands([&](eq::Bands b) { return eq::withMacro(b, k, v); });
        return ok();
    }
    if (cmd == "applyPreset") {
        const QString name = m.value("name").toString();
        return applyPreset(name) ? ok() : fail(QStringLiteral("no preset named %1").arg(name));
    }
    if (cmd == "nextPreset" || cmd == "prevPreset") {
        const QStringList list = presetSlots();
        if (list.isEmpty())
            return fail("no presets");
        const int at = int(list.indexOf(m_preset));
        const int step = cmd == "nextPreset" ? 1 : -1;
        const int next = at < 0 ? (step > 0 ? 0 : int(list.size()) - 1) : (at + step + int(list.size())) % int(list.size());
        applyPreset(list[next]);
        return ok();
    }
    if (cmd == "savePreset") {
        const QString name = m.value("name").toString().trimmed().left(120);
        if (name.isEmpty())
            return fail("a preset needs a name");
        for (const eq::Preset &p : eq::builtins())
            if (p.name.compare(name, Qt::CaseInsensitive) == 0)
                return fail(QStringLiteral("%1 is built in").arg(p.name));
        m_presets.erase(std::remove_if(m_presets.begin(), m_presets.end(), [&](const Preset &p) { return p.name == name; }),
                        m_presets.end());
        m_presets << Preset{name, "user", m_left, m_link ? m_left : m_right};
        ++m_libraryRevision;
        m_preset = name;
        m_dirty = false;
        remember(name);
        changed(false);
        return ok();
    }
    if (cmd == "deletePreset") {
        const QString name = m.value("name").toString();
        const auto before = m_presets.size();
        m_presets.erase(std::remove_if(m_presets.begin(), m_presets.end(), [&](const Preset &p) { return p.name == name; }),
                        m_presets.end());
        if (m_presets.size() == before)
            return fail(QStringLiteral("no saved preset named %1").arg(name));
        ++m_libraryRevision;
        m_recent.removeAll(name);
        for (auto it = m_pins.begin(); it != m_pins.end();)
            it = it.value() == name ? m_pins.erase(it) : std::next(it);
        changed(false);
        return ok();
    }
    if (cmd == "setChannel" || cmd == "cycleChannel") {
        QString ch = m.value("channel").toString();
        if (cmd == "cycleChannel")
            ch = m_link ? "L" : m_channel == "L" ? "R" : "L+R";
        if (ch == "L+R") {
            // Linking takes the curve being edited for both sides.
            const eq::Bands b = editing();
            m_left = m_right = b;
            m_link = true;
        } else if (ch == "L" || ch == "R") {
            if (m_link)
                m_right = m_left;
            m_link = false;
            m_channel = ch;
        } else {
            return fail("channel is L+R, L or R");
        }
        changed();
        return ok();
    }
    if (cmd == "setBypass") {
        m_bypass = m.contains("on") ? m.value("on").toBool() : !m_bypass;
        changed();
        return ok({{"bypass", m_bypass}});
    }
    if (cmd == "setOutput" || cmd == "nextOutput") {
        const QVector<Output> list = outputs();
        if (list.isEmpty())
            return fail("no outputs");
        QString name = m.value("name").toString();
        if (cmd == "nextOutput") {
            int at = 0;
            for (int i = 0; i < list.size(); ++i)
                if (list[i].name == m_output)
                    at = i;
            name = list[(at + 1) % list.size()].name;
        }
        const bool known = std::any_of(list.begin(), list.end(), [&](const Output &o) { return o.name == name; });
        if (!known)
            return fail(QStringLiteral("no output named %1").arg(name));
        chooseOutput(name, true);
        return ok();
    }
    if (cmd == "setVolume") {
        const QVector<Output> list = outputs();
        const Output *current = currentOutput(list);
        if (!current)
            return fail("no output");
        m_engine.setSinkVolume(current->volumeNode, std::clamp(m.value("v").toDouble(), 0.0, 150.0) / 100);
        return ok();
    }
    if (cmd == "setBalance") {
        m_balance = std::clamp(std::round(m.value("v").toDouble()), -100.0, 100.0);
        changed();
        return ok();
    }
    if (cmd == "setWidth") {
        m_width = std::clamp(std::round(m.value("v").toDouble()), 0.0, 200.0);
        changed();
        return ok();
    }
    if (cmd == "setRoom" || cmd == "nextRoom") {
        if (cmd == "nextRoom")
            m_room = dsp::Room((int(m_room) + 1) % RoomNames.size());
        else if (RoomNames.contains(m.value("room").toString()))
            m_room = roomFromName(m.value("room").toString());
        else
            return fail("room is off, small room, studio or hall");
        if (m_beforeRule)
            m_beforeRule->room = m_room;
        changed();
        return ok();
    }
    if (cmd == "setAppVolume" || cmd == "toggleAppMute") {
        const QString key = m.value("key").toString();
        for (const App &a : apps()) {
            if (a.key != key)
                continue;
            const bool mute = cmd == "toggleAppMute" ? !a.muted : false;
            const double v = cmd == "setAppVolume" ? std::clamp(m.value("v").toDouble(), 0.0, 100.0) / 100 : a.volume / 100.0;
            for (uint32_t s : a.streams)
                m_engine.setStreamVolume(s, v, mute);
            return ok();
        }
        return fail(QStringLiteral("no app named %1").arg(key));
    }
    // Every preset with its curve, for the presets view. The state names
    // them only, as it goes out on every change.
    if (cmd == "library") {
        QJsonArray list;
        for (const Preset &p : library())
            list.append(QJsonObject{{"name", p.name}, {"kind", p.kind}, {"L", eq::toJson(p.left)}, {"R", eq::toJson(p.right)}});
        return ok({{"presets", list}, {"revision", m_libraryRevision}});
    }
    if (cmd == "plugins") {
        QJsonArray list;
        for (const plugins::Info &i : plugins::Catalog::shared().list())
            if (plugins::Catalog::fitsStereo(i))
                list.append(QJsonObject{{"id", i.id}, {"name", pluginLabel(i.name)}, {"kind", i.kind}});
        return ok({{"plugins", list}});
    }
    if (cmd == "addPlugin") {
        // "id" also numbers a request that wants a reply, so the plugin has
        // its own field. A text "id" still comes from windows before 0.1.2.
        const QJsonValue plugin = m.contains("plugin") ? m.value("plugin") : m.value("id");
        plugins::Info info;
        if (!plugins::Catalog::shared().find(plugin.toString(), &info))
            return fail("that plugin is not installed");
        if (!plugins::Catalog::fitsStereo(info))
            return fail("that plugin is not stereo");
        m_plugins << Plugin{info.id, pluginLabel(info.name), info.kind, true, {}};
        rebuildChain();
        changed(false);
        return ok();
    }
    if (cmd == "togglePlugin" || cmd == "removePlugin" || cmd == "movePlugin") {
        const int i = m.value("i").toInt(-1);
        if (i < 0 || i >= m_plugins.size())
            return fail("no plugin at that place");
        if (cmd == "togglePlugin")
            m_plugins[i].on = !m_plugins[i].on;
        else if (cmd == "removePlugin")
            m_plugins.removeAt(i);
        else
            m_plugins.move(i, std::clamp(m.value("to").toInt(), 0, int(m_plugins.size()) - 1));
        rebuildChain();
        changed(false);
        return ok();
    }
    if (cmd == "pluginParams" || cmd == "setPluginParam") {
        const int i = m.value("i").toInt(-1);
        if (i < 0 || i >= m_plugins.size())
            return fail("no plugin at that place");
        Plugin &plugin = m_plugins[i];
        const double rate = m_engine.sampleRate() > 0 ? m_engine.sampleRate() : 48000;
        const QVector<plugins::Param> params = plugins::Catalog::shared().params(plugin.id, rate);
        if (cmd == "pluginParams") {
            QJsonArray list;
            for (const plugins::Param &p : params) {
                QJsonArray options;
                for (const auto &[value, label] : p.options)
                    options.append(QJsonObject{{"value", value}, {"label", label}});
                list.append(QJsonObject{{"symbol", p.symbol},
                                        {"name", p.name},
                                        {"min", p.min},
                                        {"max", p.max},
                                        {"default", p.def},
                                        {"value", plugin.values.value(p.symbol, p.def)},
                                        {"integer", p.integer},
                                        {"toggled", p.toggled},
                                        {"logarithmic", p.logarithmic},
                                        {"options", options}});
            }
            return ok({{"params", list}});
        }
        const QString symbol = m.value("param").toString();
        const auto param = std::find_if(params.begin(), params.end(), [&](const plugins::Param &p) { return p.symbol == symbol; });
        if (param == params.end())
            return fail(QStringLiteral("%1 has no control named %2").arg(plugin.name, symbol));
        // Without a value, the control goes back to its default.
        const float value = m.contains("value") ? plugins::clamp(*param, float(m.value("value").toDouble())) : param->def;
        if (value == param->def)
            plugin.values.remove(symbol);
        else
            plugin.values.insert(symbol, value);
        if (plugin.live)
            plugin.live->set(param->port, value);
        changed(false);
        return ok({{"value", value}});
    }
    if (cmd == "pin") {
        if (m_output.isEmpty())
            return fail("no output");
        const QString preset = m.value("preset").toString(m_preset);
        if (!findPreset(preset))
            return fail(QStringLiteral("no preset named %1").arg(preset));
        const QString output = m.value("output").toString(m_output);
        m_pins.insert(output, preset);
        // Pinning what is playing changes nothing that plays; it only marks
        // the pin as the one in force.
        if (output == m_output && !m_activeRule.startsWith("rule:"))
            m_activeRule = "pin:" + output;
        changed(false);
        return ok();
    }
    if (cmd == "unpin") {
        m_pins.remove(m.value("output").toString(m_output));
        evaluateRules();
        changed(false);
        return ok();
    }
    if (cmd == "addRule" || cmd == "updateRule") {
        Rule r;
        int i = -1;
        if (cmd == "updateRule") {
            i = m.value("index").toInt(-1);
            if (i < 0 || i >= m_rules.size())
                return fail("no rule at that place");
            r = m_rules[i];
        } else {
            r.kind = "call";
        }
        if (m.contains("kind"))
            r.kind = m.value("kind").toString();
        if (m.contains("when"))
            r.when = m.value("when").toString().left(80);
        if (m.contains("then"))
            r.then = m.value("then").toString();
        if (m.contains("room"))
            r.room = m.value("room").toString();
        if (r.kind != "call" && r.kind != "fullscreen")
            return fail("a rule is call or fullscreen");
        if (r.then.isEmpty())
            r.then = m_preset;
        if (i < 0)
            m_rules << r;
        else
            m_rules[i] = r;
        if (i >= 0 && m_activeRule == QStringLiteral("rule:%1").arg(i)) {
            applyPreset(r.then, true);
            if (!r.room.isEmpty())
                m_room = roomFromName(r.room);
        }
        evaluateRules();
        changed(false);
        return ok();
    }
    if (cmd == "removeRule") {
        const int i = m.value("index").toInt(-1);
        if (i < 0 || i >= m_rules.size())
            return fail("no rule at that place");
        m_rules.removeAt(i);
        evaluateRules();
        changed(false);
        return ok();
    }
    return fail(QStringLiteral("unknown command %1").arg(cmd));
}
