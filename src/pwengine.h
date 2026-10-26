#pragma once

// The daemon's connection to PipeWire. It publishes the "preamp" sink and
// plays what comes in through the DSP into the chosen output. It also keeps a
// picture of the graph for the daemon: outputs, app streams, links and
// defaults.
//
// PipeWire's main loop runs inside Qt's event loop, so every callback except
// the audio processing arrives on the Qt thread. The processing runs on
// PipeWire's data thread.

#include "dsp.h"

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>
#include <QVector>
#include <functional>
#include <memory>

struct pw_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct pw_stream;
struct pw_metadata;
struct pw_proxy;

class PwEngine : public QObject {
    Q_OBJECT

public:
    // "preamp" and "preamp.output". PREAMP_SINK_NAME renames both, so a test
    // daemon can run beside the real one.
    static QString sinkName();
    static QString outputName();

    struct Node {
        uint32_t id = 0;
        quint64 serial = 0;
        QString name, description, nick, mediaClass, mediaRole, mediaName;
        QString appName, appBinary, deviceApi, deviceBus, formFactor;
        uint32_t deviceId = 0;
        int profileDevice = -1; // card.profile.device, which routes refer to
        int priority = 0;
        QString state; // "running", "idle", "suspended", ...
        QVector<float> volumes;
        bool mute = false;
        bool hasVolume = false;
        int rate = 0;
        bool ours = false;

        bool isSink() const { return mediaClass == "Audio/Sink"; }
        bool isPlayback() const { return mediaClass == "Stream/Output/Audio"; }
        bool isCapture() const { return mediaClass == "Stream/Input/Audio"; }
    };

    struct Device {
        uint32_t id = 0;
        QString name, description, bus, formFactor, api;
        // Per card device index: false when every output route to it says
        // nothing is plugged in.
        QHash<int, bool> available;
    };

    struct Link {
        uint32_t id = 0;
        uint32_t outputNode = 0;
        uint32_t inputNode = 0;
    };

    explicit PwEngine(dsp::Processor *processor, QObject *parent = nullptr);
    ~PwEngine() override;

    bool start(QString *error);
    // Runs `done` once PipeWire has answered everything sent before it.
    void sync(std::function<void()> done);
    // Whether something is plugged into the jack or port a sink plays to.
    bool isAvailable(const Node &sink) const;
    bool isConnected() const { return m_core != nullptr; }

    const QHash<uint32_t, Node> &nodes() const { return m_nodes; }
    const QHash<uint32_t, Device> &devices() const { return m_devices; }
    const QHash<uint32_t, Link> &links() const { return m_links; }
    QString defaultSink() const { return m_defaultSink; }
    QString configuredSink() const { return m_configuredSink; }
    QString targetOf(uint32_t nodeId) const { return m_targets.value(nodeId); }
    uint32_t sinkNodeId() const;
    uint32_t outputNodeId() const;
    double sampleRate() const { return m_rate; }

    // The sink our output stream is linked to right now, or 0.
    uint32_t linkedOutput() const;
    // Where an app stream plays into right now, or 0.
    uint32_t linkedSinkOf(uint32_t streamId) const;

    void setOutputTarget(const QString &sinkName);
    void setConfiguredDefaultSink(const QString &sinkName);
    void clearTarget(uint32_t nodeId);
    void setStreamVolume(uint32_t nodeId, double volume, bool mute);
    void setSinkVolume(uint32_t nodeId, double volume);

signals:
    void graphChanged();
    void defaultsChanged();
    void rateChanged(double rate);
    void disconnected();

private:
    struct Bound;
    friend struct PwCallbacks;

    void iterate();
    void scheduleChanged();
    void onGlobal(uint32_t id, const char *type, const struct spa_dict *props);
    void onGlobalRemove(uint32_t id);
    void onMetadata(uint32_t subject, const char *key, const char *type, const char *value);
    void onCaptureFormat(const struct spa_pod *param);
    void processAudio();
    void runVolume();
    void teardown();

    dsp::Processor *m_processor;
    pw_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_registry *m_registry = nullptr;
    pw_stream *m_capture = nullptr;
    pw_stream *m_playback = nullptr;
    pw_metadata *m_metadata = nullptr;
    pw_proxy *m_metadataProxy = nullptr;
    std::unique_ptr<QSocketNotifier> m_notifier;

    struct Hooks;
    std::unique_ptr<Hooks> m_hooks;
    QHash<uint32_t, Bound *> m_bound;

    QHash<uint32_t, Node> m_nodes;
    QHash<uint32_t, Device> m_devices;
    QHash<uint32_t, Link> m_links;
    QHash<uint32_t, QString> m_targets;
    QString m_defaultSink, m_configuredSink;
    QString m_initialTarget;
    double m_rate = 0;
    QTimer m_changed;
    QHash<int, std::function<void()>> m_syncs;

    // Sink volume goes through wpctl, which knows about hardware routes. One
    // call at a time, the newest value wins.
    QProcess m_volumeProcess;
    uint32_t m_volumeNode = 0;
    double m_volumeValue = -1;
};
