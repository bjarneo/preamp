#pragma once

// The background half of preamp. It owns the equalizer and everything around
// it, keeps the preamp sink as the system output while playing into the
// output the user picked, switches presets by rule, and serves the window,
// the CLI and the shell plugin over the socket.

#include "analyzer.h"
#include "autoeq.h"
#include "dsp.h"
#include "eq.h"
#include "hyprland.h"
#include "ipc.h"
#include "pwengine.h"
#include "theme.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

class Daemon : public QObject {
    Q_OBJECT

public:
    explicit Daemon(const QString &configDir, const QString &cacheDir, QObject *parent = nullptr);
    ~Daemon() override;

    bool start(QString *error);
    // Hands the system output back to the device preamp was playing into.
    void stop();

    QJsonObject state() const;
    QJsonObject handle(const QJsonObject &message);

    static QStringList rooms();

private:
    struct Preset {
        QString name, kind;
        eq::Bands left, right;
    };
    struct Rule {
        QString kind; // "call" or "fullscreen"
        QString when; // app name
        QString then; // preset name
        QString room; // optional room while the rule holds
    };
    struct Plugin {
        QString id, name, kind;
        bool on = false;
        QString error;
        plugins::Values values = {}; // the controls set away from their default
        // In the newest chain posted to the processor, which outlives this
        // pointer: rebuildChain() replaces it before that chain can be freed.
        plugins::Instance *live = nullptr;
    };
    struct Output {
        QString name;   // node.name to target
        QString label;  // what people call it
        QString kind;   // usb, bluetooth, pci, hdmi, virtual
        QString rate;   // "48 kHz"
        uint32_t id = 0;
        uint32_t volumeNode = 0; // where loudness lives, through any tuning
        int priority = 0;
        bool hardware = false; // a device, not a virtual sink
    };
    struct App {
        QString key, name, state;
        QVector<uint32_t> streams;
        int volume = 100;
        bool muted = false;
        bool inCall = false;
    };

    // Model.
    void load();
    void scheduleSave();
    void save();
    void changed(bool dsp = true);
    void pushDsp();
    void rebuildChain();
    eq::Bands &editing();
    void setBands(const std::function<eq::Bands(eq::Bands)> &edit);
    bool applyPreset(const QString &name, bool fromRule = false);
    const Preset *findPreset(const QString &name) const;
    QVector<Preset> library() const;
    QStringList presetSlots() const;
    void remember(const QString &name);

    // Graph.
    void routeFirstTime();
    void onGraph();
    void onDefaults();
    QVector<Output> outputs() const;
    const Output *currentOutput(const QVector<Output> &list) const;
    QVector<App> apps() const;
    void chooseOutput(const QString &name, bool byUser);
    void reclaimStreams(const QString &from);
    QString desiredOutput(const QVector<Output> &list) const;

    // Rules.
    void evaluateRules();
    QJsonArray rulesJson() const;

    // Clients.
    void onMessage(ipc::Connection *from, const QJsonObject &message);
    void broadcastState();
    // Starts the state timer unless it already runs, so a stream of
    // changes during a drag still sends state every 16 ms.
    void queueState();
    void tickSpectrum();
    void notify(const QString &summary, const QString &body);
    void search(const QString &query, ipc::Connection *from, int id);
    void refreshIcon();

    QString m_configDir, m_cacheDir;

    eq::Bands m_left, m_right;
    bool m_link = true;
    QString m_channel = "L";
    QString m_preset = "Flat";
    bool m_dirty = false;
    bool m_bypass = false;
    QVector<Preset> m_presets;
    // Goes up when a preset is added, replaced or deleted, so the window
    // knows to fetch the library again.
    int m_libraryRevision = 0;
    QStringList m_recent;
    QHash<QString, QString> m_pins;
    QHash<QString, QString> m_outputLabels;
    QStringList m_history;
    QVector<Rule> m_rules;
    QVector<Plugin> m_plugins;
    dsp::Room m_room = dsp::Room::Off;
    double m_width = 100;
    double m_balance = 0;
    QString m_output;
    bool m_seenFirstRun = false;

    dsp::Processor m_processor;
    PwEngine m_engine;
    ipc::Server m_server;
    AutoEq m_autoeq;
    Hyprland m_hyprland;
    Theme m_theme;
    Analyzer m_analyzer;

    bool m_routed = false;
    bool m_stopping = false;
    QString m_activeRule;
    struct Snapshot {
        eq::Bands left, right;
        QString preset;
        bool dirty = false;
        dsp::Room room = dsp::Room::Off;
    };
    std::optional<Snapshot> m_beforeRule;
    QSet<QString> m_knownSinks;
    QString m_pinApplied;

    QTimer m_stateTimer, m_saveTimer, m_spectrumTimer, m_collectTimer, m_graphTimer;
    QList<QPair<QPointer<ipc::Connection>, QPair<QString, int>>> m_pendingSearches;
};
