#include "pwengine.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <pipewire/extensions/metadata.h>
#include <pipewire/impl.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/utils/result.h>

struct PwEngine::Hooks {
    spa_hook core{};
    spa_hook registry{};
    spa_hook metadata{};
    spa_hook capture{};
    spa_hook playback{};
};

struct PwEngine::Bound {
    PwEngine *engine = nullptr;
    uint32_t id = 0;
    bool node = false;
    pw_proxy *proxy = nullptr;
    spa_hook proxyHook{};
    spa_hook objectHook{};
};

namespace {

QString prop(const spa_dict *props, const char *key) {
    const char *v = props ? spa_dict_lookup(props, key) : nullptr;
    return v ? QString::fromUtf8(v) : QString();
}

// "{ "name": "alsa_output..." }" from the default metadata.
QString nameFromJson(const char *value) {
    if (!value)
        return {};
    return QJsonDocument::fromJson(QByteArray(value)).object().value("name").toString();
}

} // namespace

// The C callbacks, gathered in one friend so they can reach the engine.
struct PwCallbacks {
    static void coreError(void *data, uint32_t id, int, int res, const char *message) {
        auto *e = static_cast<PwEngine *>(data);
        // Errors on other objects are answers to requests that may fail, like
        // the format of a suspended device; only the core's own matter.
        if (id != PW_ID_CORE)
            return;
        qWarning("pipewire: %s (%s)", message ? message : "error", spa_strerror(res));
        if (res == -EPIPE)
            QMetaObject::invokeMethod(e, [e] {
                e->teardown();
                emit e->disconnected();
            }, Qt::QueuedConnection);
    }

    static void global(void *data, uint32_t id, uint32_t, const char *type, uint32_t, const spa_dict *props) {
        static_cast<PwEngine *>(data)->onGlobal(id, type, props);
    }

    static void globalRemove(void *data, uint32_t id) {
        static_cast<PwEngine *>(data)->onGlobalRemove(id);
    }

    static void proxyRemoved(void *data) {
        pw_proxy_destroy(static_cast<PwEngine::Bound *>(data)->proxy);
    }

    static void proxyDestroy(void *data) {
        auto *b = static_cast<PwEngine::Bound *>(data);
        spa_hook_remove(&b->proxyHook);
        spa_hook_remove(&b->objectHook);
        b->engine->m_bound.remove(b->id);
        delete b;
    }

    static void nodeInfo(void *data, const pw_node_info *info) {
        auto *b = static_cast<PwEngine::Bound *>(data);
        PwEngine *e = b->engine;
        auto it = e->m_nodes.find(b->id);
        if (it == e->m_nodes.end())
            return;
        PwEngine::Node &n = *it;
        if (info->change_mask & PW_NODE_CHANGE_MASK_STATE)
            n.state = QString::fromUtf8(pw_node_state_as_string(info->state));
        if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) {
            const spa_dict *p = info->props;
            n.name = prop(p, PW_KEY_NODE_NAME);
            n.description = prop(p, PW_KEY_NODE_DESCRIPTION);
            n.nick = prop(p, PW_KEY_NODE_NICK);
            n.mediaClass = prop(p, PW_KEY_MEDIA_CLASS);
            n.mediaRole = prop(p, PW_KEY_MEDIA_ROLE);
            n.mediaName = prop(p, PW_KEY_MEDIA_NAME);
            n.appName = prop(p, PW_KEY_APP_NAME);
            n.appBinary = prop(p, PW_KEY_APP_PROCESS_BINARY);
            n.deviceApi = prop(p, PW_KEY_DEVICE_API);
            n.deviceId = prop(p, PW_KEY_DEVICE_ID).toUInt();
            const QString profileDevice = prop(p, "card.profile.device");
            n.profileDevice = profileDevice.isEmpty() ? -1 : profileDevice.toInt();
            n.priority = prop(p, PW_KEY_PRIORITY_SESSION).toInt();
            n.serial = prop(p, PW_KEY_OBJECT_SERIAL).toULongLong();
            const QString rate = prop(p, "audio.rate");
            if (!rate.isEmpty())
                n.rate = rate.toInt();
        }
        e->scheduleChanged();
    }

    static void nodeParam(void *data, int, uint32_t id, uint32_t, uint32_t, const spa_pod *param) {
        auto *b = static_cast<PwEngine::Bound *>(data);
        PwEngine *e = b->engine;
        auto it = e->m_nodes.find(b->id);
        if (it == e->m_nodes.end() || !param)
            return;
        PwEngine::Node &n = *it;
        if (id == SPA_PARAM_Props && spa_pod_is_object(param)) {
            const auto *obj = reinterpret_cast<const spa_pod_object *>(param);
            const spa_pod_prop *p;
            SPA_POD_OBJECT_FOREACH(obj, p) {
                if (p->key == SPA_PROP_channelVolumes) {
                    float vols[SPA_AUDIO_MAX_CHANNELS];
                    const uint32_t count = spa_pod_copy_array(&p->value, SPA_TYPE_Float, vols, SPA_AUDIO_MAX_CHANNELS);
                    if (count > 0) {
                        n.volumes = QVector<float>(vols, vols + count);
                        n.hasVolume = true;
                    }
                } else if (p->key == SPA_PROP_mute) {
                    bool mute = false;
                    if (spa_pod_get_bool(&p->value, &mute) == 0)
                        n.mute = mute;
                }
            }
            e->scheduleChanged();
        } else if (id == SPA_PARAM_Format) {
            uint32_t mediaType = 0, mediaSubtype = 0;
            if (spa_format_parse(param, &mediaType, &mediaSubtype) >= 0 && mediaType == SPA_MEDIA_TYPE_audio &&
                mediaSubtype == SPA_MEDIA_SUBTYPE_raw) {
                spa_audio_info_raw raw{};
                if (spa_format_audio_raw_parse(param, &raw) >= 0 && raw.rate > 0) {
                    n.rate = int(raw.rate);
                    e->scheduleChanged();
                }
            }
        }
    }

    static void deviceInfo(void *data, const pw_device_info *info) {
        auto *b = static_cast<PwEngine::Bound *>(data);
        PwEngine *e = b->engine;
        if (!(info->change_mask & PW_DEVICE_CHANGE_MASK_PROPS))
            return;
        PwEngine::Device &d = e->m_devices[b->id];
        d.id = b->id;
        d.name = prop(info->props, PW_KEY_DEVICE_NAME);
        d.description = prop(info->props, PW_KEY_DEVICE_DESCRIPTION);
        d.bus = prop(info->props, PW_KEY_DEVICE_BUS);
        d.formFactor = prop(info->props, PW_KEY_DEVICE_FORM_FACTOR);
        d.api = prop(info->props, PW_KEY_DEVICE_API);
        e->scheduleChanged();
    }

    static void deviceParam(void *data, int, uint32_t id, uint32_t index, uint32_t, const spa_pod *param) {
        auto *b = static_cast<PwEngine::Bound *>(data);
        if (id != SPA_PARAM_EnumRoute || !param || !spa_pod_is_object(param))
            return;
        PwEngine::Device &d = b->engine->m_devices[b->id];
        d.id = b->id;
        // Routes arrive one per event, from index 0 whenever they change.
        if (index == 0)
            d.available.clear();
        uint32_t direction = SPA_DIRECTION_INPUT, availability = SPA_PARAM_AVAILABILITY_unknown;
        int devices[64];
        uint32_t deviceCount = 0;
        const auto *obj = reinterpret_cast<const spa_pod_object *>(param);
        const spa_pod_prop *p;
        SPA_POD_OBJECT_FOREACH(obj, p) {
            if (p->key == SPA_PARAM_ROUTE_direction)
                spa_pod_get_id(&p->value, &direction);
            else if (p->key == SPA_PARAM_ROUTE_available)
                spa_pod_get_id(&p->value, &availability);
            else if (p->key == SPA_PARAM_ROUTE_devices)
                deviceCount = spa_pod_copy_array(&p->value, SPA_TYPE_Int, devices, 64);
        }
        if (direction != SPA_DIRECTION_OUTPUT)
            return;
        for (uint32_t i = 0; i < deviceCount; ++i) {
            const bool yes = availability != SPA_PARAM_AVAILABILITY_no;
            d.available[devices[i]] = d.available.value(devices[i], false) || yes;
        }
        b->engine->scheduleChanged();
    }

    static void coreDone(void *data, uint32_t id, int seq) {
        auto *e = static_cast<PwEngine *>(data);
        if (id != PW_ID_CORE)
            return;
        if (auto done = e->m_syncs.take(seq))
            QMetaObject::invokeMethod(e, done, Qt::QueuedConnection);
    }

    static int metadataProperty(void *data, uint32_t subject, const char *key, const char *type, const char *value) {
        static_cast<PwEngine *>(data)->onMetadata(subject, key, type, value);
        return 0;
    }

    static void captureProcess(void *data) {
        auto *e = static_cast<PwEngine *>(data);
        // The output stream runs right after this one in the same cycle. When
        // it is not ready, recycle the input so the sink keeps moving.
        if (pw_stream_trigger_process(e->m_playback) < 0) {
            while (pw_buffer *b = pw_stream_dequeue_buffer(e->m_capture))
                pw_stream_queue_buffer(e->m_capture, b);
        }
    }

    static void playbackProcess(void *data) {
        static_cast<PwEngine *>(data)->processAudio();
    }

    static void captureParamChanged(void *data, uint32_t id, const spa_pod *param) {
        if (id == SPA_PARAM_Format && param)
            static_cast<PwEngine *>(data)->onCaptureFormat(param);
    }

    static void streamState(void *data, pw_stream_state, pw_stream_state state, const char *error) {
        auto *e = static_cast<PwEngine *>(data);
        if (state == PW_STREAM_STATE_ERROR)
            qWarning("pipewire stream: %s", error ? error : "error");
        e->scheduleChanged();
    }

    // The output stream stays at full level and unmuted. A volume left on it
    // by a mixer or a restored session cuts every sound without a trace in
    // preamp, whose volume lives on the output device.
    static void playbackControl(void *data, uint32_t id, const pw_stream_control *control) {
        if (!control || control->n_values == 0)
            return;
        bool full = true;
        if (id == SPA_PROP_volume || id == SPA_PROP_channelVolumes) {
            for (uint32_t i = 0; i < control->n_values; ++i)
                full = full && std::abs(control->values[i] - 1.0f) < 1e-4f;
        } else if (id == SPA_PROP_mute) {
            full = control->values[0] == 0.0f;
        } else {
            return;
        }
        if (full)
            return;
        auto *e = static_cast<PwEngine *>(data);
        const uint32_t count = std::min<uint32_t>(control->n_values, SPA_AUDIO_MAX_CHANNELS);
        // Set from the event loop, not from inside PipeWire's callback.
        QMetaObject::invokeMethod(e, [e, id, count] {
            if (!e->m_playback)
                return;
            float values[SPA_AUDIO_MAX_CHANNELS];
            std::fill_n(values, count, id == SPA_PROP_mute ? 0.0f : 1.0f);
            pw_stream_set_control(e->m_playback, id, count, values, 0);
        }, Qt::QueuedConnection);
    }
};

namespace {

// Built field by field: the structs grow with each PipeWire release, and
// whatever is not set stays null.
const pw_core_events CoreEvents = [] {
    pw_core_events e{};
    e.version = PW_VERSION_CORE_EVENTS;
    e.done = PwCallbacks::coreDone;
    e.error = PwCallbacks::coreError;
    return e;
}();

const pw_registry_events RegistryEvents = [] {
    pw_registry_events e{};
    e.version = PW_VERSION_REGISTRY_EVENTS;
    e.global = PwCallbacks::global;
    e.global_remove = PwCallbacks::globalRemove;
    return e;
}();

const pw_proxy_events ProxyEvents = [] {
    pw_proxy_events e{};
    e.version = PW_VERSION_PROXY_EVENTS;
    e.destroy = PwCallbacks::proxyDestroy;
    e.removed = PwCallbacks::proxyRemoved;
    return e;
}();

const pw_node_events NodeEvents = [] {
    pw_node_events e{};
    e.version = PW_VERSION_NODE_EVENTS;
    e.info = PwCallbacks::nodeInfo;
    e.param = PwCallbacks::nodeParam;
    return e;
}();

const pw_device_events DeviceEvents = [] {
    pw_device_events e{};
    e.version = PW_VERSION_DEVICE_EVENTS;
    e.info = PwCallbacks::deviceInfo;
    e.param = PwCallbacks::deviceParam;
    return e;
}();

const pw_metadata_events MetadataEvents = [] {
    pw_metadata_events e{};
    e.version = PW_VERSION_METADATA_EVENTS;
    e.property = PwCallbacks::metadataProperty;
    return e;
}();

const pw_stream_events CaptureEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.state_changed = PwCallbacks::streamState;
    e.param_changed = PwCallbacks::captureParamChanged;
    e.process = PwCallbacks::captureProcess;
    return e;
}();

const pw_stream_events PlaybackEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.state_changed = PwCallbacks::streamState;
    e.control_info = PwCallbacks::playbackControl;
    e.process = PwCallbacks::playbackProcess;
    return e;
}();

} // namespace

QString PwEngine::sinkName() {
    static const QString name = qEnvironmentVariable("PREAMP_SINK_NAME", "preamp");
    return name;
}

QString PwEngine::outputName() { return sinkName() + ".output"; }

PwEngine::PwEngine(dsp::Processor *processor, QObject *parent)
    : QObject(parent), m_processor(processor), m_hooks(std::make_unique<Hooks>()) {
    pw_init(nullptr, nullptr);
    m_changed.setSingleShot(true);
    m_changed.setInterval(30);
    connect(&m_changed, &QTimer::timeout, this, &PwEngine::graphChanged);
    connect(&m_volumeProcess, &QProcess::finished, this, &PwEngine::runVolume);
    connect(&m_volumeProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart)
            qWarning("preamp: wpctl is missing, so the output volume cannot change");
    });
}

PwEngine::~PwEngine() {
    disconnect(&m_volumeProcess, nullptr, this, nullptr);
    teardown();
    pw_deinit();
}

bool PwEngine::start(QString *error) {
    m_loop = pw_loop_new(nullptr);
    if (!m_loop) {
        *error = QStringLiteral("could not create a PipeWire loop");
        return false;
    }
    pw_loop_enter(m_loop);
    m_notifier = std::make_unique<QSocketNotifier>(pw_loop_get_fd(m_loop), QSocketNotifier::Read);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, &PwEngine::iterate);

    // Ask RTKit for the realtime audio thread directly, as the PipeWire
    // daemon does. A desktop portal that started before rtkit was installed
    // reports a realtime budget of zero. PipeWire applies it as a hard limit
    // of 0, and the kernel then kills preamp as soon as audio plays. The
    // client config asks through the portal, so its realtime module stays out.
    m_context = pw_context_new(m_loop, pw_properties_new(PW_KEY_APP_NAME, "preamp", "module.rt", "false", nullptr), 0);
    if (!m_context) {
        *error = QStringLiteral("could not create a PipeWire context");
        return false;
    }
    // Without rtkit the module does not load, and preamp runs without
    // realtime priority.
    pw_context_load_module(m_context, "libpipewire-module-rt", "{ rtportal.enabled = false }", nullptr);
    m_core = pw_context_connect(m_context, nullptr, 0);
    if (!m_core) {
        *error = QStringLiteral("PipeWire is not running");
        return false;
    }
    pw_core_add_listener(m_core, &m_hooks->core, &CoreEvents, this);
    m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(m_registry, &m_hooks->registry, &RegistryEvents, this);

    // The sink apps play into. node.group keeps it and the output stream in
    // one cycle; node.link-group stops the session manager from looping the
    // output back into it.
    pw_properties *in = pw_properties_new(
        PW_KEY_MEDIA_CLASS, "Audio/Sink",
        PW_KEY_NODE_NAME, sinkName().toUtf8().constData(),
        PW_KEY_NODE_DESCRIPTION, "preamp",
        PW_KEY_NODE_NICK, "preamp",
        PW_KEY_MEDIA_NAME, "preamp",
        PW_KEY_NODE_GROUP, "preamp",
        PW_KEY_NODE_LINK_GROUP, "preamp",
        PW_KEY_NODE_VIRTUAL, "true",
        PW_KEY_AUDIO_CHANNELS, "2",
        SPA_KEY_AUDIO_POSITION, "[ FL FR ]",
        "resample.prefill", "true",
        "device.icon-name", "preamp",
        nullptr);
    m_capture = pw_stream_new(m_core, "preamp", in);

    // The stream into the speakers or headphones. Passive, so an idle sink
    // lets the device suspend. WirePlumber neither saves nor restores its
    // volume, as it must stay at full level.
    pw_properties *out = pw_properties_new(
        PW_KEY_NODE_NAME, outputName().toUtf8().constData(),
        PW_KEY_NODE_DESCRIPTION, "preamp output",
        PW_KEY_MEDIA_NAME, "preamp output",
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_NODE_GROUP, "preamp",
        PW_KEY_NODE_LINK_GROUP, "preamp",
        PW_KEY_NODE_PASSIVE, "true",
        PW_KEY_NODE_VIRTUAL, "true",
        PW_KEY_AUDIO_CHANNELS, "2",
        SPA_KEY_AUDIO_POSITION, "[ FL FR ]",
        "resample.prefill", "true",
        "state.restore-props", "false",
        nullptr);
    if (!m_initialTarget.isEmpty())
        pw_properties_set(out, PW_KEY_TARGET_OBJECT, m_initialTarget.toUtf8().constData());
    m_playback = pw_stream_new(m_core, "preamp output", out);

    pw_stream_add_listener(m_capture, &m_hooks->capture, &CaptureEvents, this);
    pw_stream_add_listener(m_playback, &m_hooks->playback, &PlaybackEvents, this);

    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32P;
    info.channels = 2;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};

    // As PipeWire's own loopback does it: the output first, so it is active
    // before the sink triggers it, and the sink asynchronous, because its
    // buffers are taken and handed back from the output's process call.
    const auto flags = pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);
    if (pw_stream_connect(m_playback, PW_DIRECTION_OUTPUT, PW_ID_ANY, pw_stream_flags(flags | PW_STREAM_FLAG_TRIGGER),
                          params, 1) < 0 ||
        pw_stream_connect(m_capture, PW_DIRECTION_INPUT, PW_ID_ANY, pw_stream_flags(flags | PW_STREAM_FLAG_ASYNC),
                          params, 1) < 0) {
        *error = QStringLiteral("could not create the preamp sink");
        return false;
    }
    iterate();
    return true;
}

void PwEngine::sync(std::function<void()> done) {
    if (!m_core) {
        QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    const int seq = pw_core_sync(m_core, PW_ID_CORE, 0);
    m_syncs.insert(seq, std::move(done));
    iterate();
}

bool PwEngine::isAvailable(const Node &sink) const {
    if (sink.deviceId == 0 || sink.profileDevice < 0)
        return true;
    const auto it = m_devices.constFind(sink.deviceId);
    if (it == m_devices.constEnd() || !it->available.contains(sink.profileDevice))
        return true;
    return it->available.value(sink.profileDevice);
}

void PwEngine::teardown() {
    m_notifier.reset();
    if (m_capture) {
        pw_stream_destroy(m_capture);
        m_capture = nullptr;
    }
    if (m_playback) {
        pw_stream_destroy(m_playback);
        m_playback = nullptr;
    }
    const auto bound = m_bound.values();
    for (Bound *b : bound)
        pw_proxy_destroy(b->proxy);
    m_bound.clear();
    if (m_metadataProxy) {
        spa_hook_remove(&m_hooks->metadata);
        pw_proxy_destroy(m_metadataProxy);
        m_metadataProxy = nullptr;
        m_metadata = nullptr;
    }
    if (m_registry) {
        spa_hook_remove(&m_hooks->registry);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_registry));
        m_registry = nullptr;
    }
    if (m_core) {
        spa_hook_remove(&m_hooks->core);
        pw_core_disconnect(m_core);
        m_core = nullptr;
    }
    if (m_context) {
        pw_context_destroy(m_context);
        m_context = nullptr;
    }
    if (m_loop) {
        pw_loop_leave(m_loop);
        pw_loop_destroy(m_loop);
        m_loop = nullptr;
    }
    m_nodes.clear();
    m_devices.clear();
    m_links.clear();
    m_targets.clear();
}

void PwEngine::iterate() {
    if (m_loop)
        pw_loop_iterate(m_loop, 0);
}

void PwEngine::scheduleChanged() {
    if (!m_changed.isActive())
        m_changed.start();
}

void PwEngine::onGlobal(uint32_t id, const char *type, const spa_dict *props) {
    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        const QString mediaClass = prop(props, PW_KEY_MEDIA_CLASS);
        if (!mediaClass.contains("Audio"))
            return;
        Node n;
        n.id = id;
        n.name = prop(props, PW_KEY_NODE_NAME);
        n.mediaClass = mediaClass;
        n.serial = prop(props, PW_KEY_OBJECT_SERIAL).toULongLong();
        m_nodes.insert(id, n);

        auto *b = new Bound;
        b->engine = this;
        b->id = id;
        b->node = true;
        b->proxy = static_cast<pw_proxy *>(pw_registry_bind(m_registry, id, type, PW_VERSION_NODE, 0));
        if (!b->proxy) {
            delete b;
            return;
        }
        m_bound.insert(id, b);
        pw_proxy_add_listener(b->proxy, &b->proxyHook, &ProxyEvents, b);
        pw_proxy_add_object_listener(b->proxy, &b->objectHook, &NodeEvents, b);
        uint32_t ids[] = {SPA_PARAM_Props, SPA_PARAM_Format};
        pw_node_subscribe_params(reinterpret_cast<pw_node *>(b->proxy), ids, 2);
        scheduleChanged();
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Device) == 0) {
        if (!prop(props, PW_KEY_MEDIA_CLASS).contains("Audio"))
            return;
        auto *b = new Bound;
        b->engine = this;
        b->id = id;
        b->proxy = static_cast<pw_proxy *>(pw_registry_bind(m_registry, id, type, PW_VERSION_DEVICE, 0));
        if (!b->proxy) {
            delete b;
            return;
        }
        m_bound.insert(id, b);
        pw_proxy_add_listener(b->proxy, &b->proxyHook, &ProxyEvents, b);
        pw_proxy_add_object_listener(b->proxy, &b->objectHook, &DeviceEvents, b);
        uint32_t ids[] = {SPA_PARAM_EnumRoute};
        pw_device_subscribe_params(reinterpret_cast<pw_device *>(b->proxy), ids, 1);
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        Link l;
        l.id = id;
        l.outputNode = prop(props, PW_KEY_LINK_OUTPUT_NODE).toUInt();
        l.inputNode = prop(props, PW_KEY_LINK_INPUT_NODE).toUInt();
        m_links.insert(id, l);
        scheduleChanged();
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        if (prop(props, PW_KEY_METADATA_NAME) != "default" || m_metadataProxy)
            return;
        m_metadataProxy = static_cast<pw_proxy *>(pw_registry_bind(m_registry, id, type, PW_VERSION_METADATA, 0));
        m_metadata = reinterpret_cast<pw_metadata *>(m_metadataProxy);
        pw_metadata_add_listener(m_metadata, &m_hooks->metadata, &MetadataEvents, this);
    }
}

void PwEngine::onGlobalRemove(uint32_t id) {
    if (Bound *b = m_bound.value(id))
        pw_proxy_destroy(b->proxy);
    const bool known = m_nodes.remove(id) + m_devices.remove(id) + m_links.remove(id) > 0;
    m_targets.remove(id);
    if (known)
        scheduleChanged();
}

void PwEngine::onMetadata(uint32_t subject, const char *key, const char *type, const char *value) {
    Q_UNUSED(type);
    if (!key) {
        // Everything for this subject was cleared.
        if (subject == PW_ID_CORE) {
            m_defaultSink.clear();
            m_configuredSink.clear();
            emit defaultsChanged();
        } else {
            m_targets.remove(subject);
        }
        return;
    }
    if (subject == PW_ID_CORE) {
        if (std::strcmp(key, "default.audio.sink") == 0) {
            m_defaultSink = nameFromJson(value);
            emit defaultsChanged();
        } else if (std::strcmp(key, "default.configured.audio.sink") == 0) {
            m_configuredSink = nameFromJson(value);
            emit defaultsChanged();
        }
        return;
    }
    if (std::strcmp(key, "target.object") == 0 || std::strcmp(key, "target.node") == 0) {
        if (value)
            m_targets.insert(subject, QString::fromUtf8(value));
        else
            m_targets.remove(subject);
    }
}

void PwEngine::onCaptureFormat(const spa_pod *param) {
    spa_audio_info_raw raw{};
    uint32_t mediaType = 0, mediaSubtype = 0;
    if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0 || mediaType != SPA_MEDIA_TYPE_audio ||
        mediaSubtype != SPA_MEDIA_SUBTYPE_raw || spa_format_audio_raw_parse(param, &raw) < 0 || raw.rate == 0)
        return;
    if (raw.rate != m_rate) {
        m_rate = raw.rate;
        emit rateChanged(m_rate);
    }
}

void PwEngine::processAudio() {
    pw_buffer *in = nullptr;
    while (pw_buffer *t = pw_stream_dequeue_buffer(m_capture)) {
        if (in)
            pw_stream_queue_buffer(m_capture, in);
        in = t;
    }
    pw_buffer *out = pw_stream_dequeue_buffer(m_playback);
    if (out) {
        spa_buffer *ob = out->buffer;
        uint32_t frames = UINT32_MAX;
        float *dst[2];
        for (uint32_t i = 0; i < 2; ++i) {
            spa_data &d = ob->datas[std::min(i, ob->n_datas - 1)];
            dst[i] = static_cast<float *>(d.data);
            frames = std::min(frames, d.maxsize / uint32_t(sizeof(float)));
        }
        if (in && dst[0] && dst[1]) {
            spa_buffer *ib = in->buffer;
            const float *src[2];
            uint32_t available = UINT32_MAX;
            for (uint32_t i = 0; i < 2; ++i) {
                spa_data &d = ib->datas[std::min(i, ib->n_datas - 1)];
                const uint32_t offset = std::min(d.chunk->offset, d.maxsize);
                const uint32_t size = std::min(d.chunk->size, d.maxsize - offset);
                src[i] = d.data ? SPA_PTROFF(d.data, offset, const float) : nullptr;
                available = std::min(available, size / uint32_t(sizeof(float)));
            }
            frames = std::min(frames, available);
            if (src[0] && src[1])
                m_processor->process(src, dst, frames);
            else
                frames = 0;
        } else {
            // Nothing came in this cycle: play silence rather than old data.
            frames = std::min<uint32_t>(frames, out->requested ? uint32_t(out->requested) : 1024);
            for (uint32_t i = 0; i < 2; ++i)
                if (dst[i])
                    std::memset(dst[i], 0, frames * sizeof(float));
        }
        for (uint32_t i = 0; i < ob->n_datas; ++i) {
            ob->datas[i].chunk->offset = 0;
            ob->datas[i].chunk->size = frames * sizeof(float);
            ob->datas[i].chunk->stride = sizeof(float);
        }
        pw_stream_queue_buffer(m_playback, out);
    }
    if (in)
        pw_stream_queue_buffer(m_capture, in);
}

uint32_t PwEngine::sinkNodeId() const {
    return m_capture ? pw_stream_get_node_id(m_capture) : SPA_ID_INVALID;
}

uint32_t PwEngine::outputNodeId() const {
    return m_playback ? pw_stream_get_node_id(m_playback) : SPA_ID_INVALID;
}

uint32_t PwEngine::linkedOutput() const {
    const uint32_t id = outputNodeId();
    for (const Link &l : m_links)
        if (l.outputNode == id && m_nodes.value(l.inputNode).isSink())
            return l.inputNode;
    return 0;
}

uint32_t PwEngine::linkedSinkOf(uint32_t streamId) const {
    for (const Link &l : m_links)
        if (l.outputNode == streamId && m_nodes.contains(l.inputNode))
            return l.inputNode;
    return 0;
}

void PwEngine::setOutputTarget(const QString &sinkName) {
    const uint32_t id = outputNodeId();
    if (!m_playback || id == SPA_ID_INVALID || !m_metadata) {
        m_initialTarget = sinkName;
        return;
    }
    pw_metadata_set_property(m_metadata, id, "target.object", "Spa:String", sinkName.toUtf8().constData());
    iterate();
}

void PwEngine::setConfiguredDefaultSink(const QString &sinkName) {
    if (!m_metadata)
        return;
    const QByteArray json = QJsonDocument(QJsonObject{{"name", sinkName}}).toJson(QJsonDocument::Compact);
    pw_metadata_set_property(m_metadata, PW_ID_CORE, "default.configured.audio.sink", "Spa:String:JSON", json.constData());
    iterate();
}

void PwEngine::clearTarget(uint32_t nodeId) {
    if (!m_metadata)
        return;
    pw_metadata_set_property(m_metadata, nodeId, "target.object", nullptr, nullptr);
    pw_metadata_set_property(m_metadata, nodeId, "target.node", nullptr, nullptr);
    iterate();
}

void PwEngine::setStreamVolume(uint32_t nodeId, double volume, bool mute) {
    Bound *b = m_bound.value(nodeId);
    const Node node = m_nodes.value(nodeId);
    if (!b || !b->node)
        return;
    // The slider is on the cubic scale pactl and wpctl show; the node wants
    // linear gain per channel.
    const float linear = float(std::pow(std::clamp(volume, 0.0, 1.5), 3));
    const uint32_t channels = std::max<uint32_t>(1, std::min<uint32_t>(uint32_t(node.volumes.size()), SPA_AUDIO_MAX_CHANNELS));
    float vols[SPA_AUDIO_MAX_CHANNELS];
    std::fill(vols, vols + channels, linear);

    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod_frame frame;
    spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    spa_pod_builder_prop(&builder, SPA_PROP_channelVolumes, 0);
    spa_pod_builder_array(&builder, sizeof(float), SPA_TYPE_Float, channels, vols);
    spa_pod_builder_prop(&builder, SPA_PROP_mute, 0);
    spa_pod_builder_bool(&builder, mute);
    const auto *pod = static_cast<const spa_pod *>(spa_pod_builder_pop(&builder, &frame));
    pw_node_set_param(reinterpret_cast<pw_node *>(b->proxy), SPA_PARAM_Props, 0, pod);
    iterate();
}

void PwEngine::setSinkVolume(uint32_t nodeId, double volume) {
    m_volumeNode = nodeId;
    m_volumeValue = std::clamp(volume, 0.0, 1.5);
    if (m_volumeProcess.state() == QProcess::NotRunning)
        runVolume();
}

void PwEngine::runVolume() {
    if (m_volumeValue < 0 || m_volumeNode == 0)
        return;
    const QStringList args{"set-volume", "-l", "1.5", QString::number(m_volumeNode), QString::number(m_volumeValue, 'f', 4)};
    m_volumeValue = -1;
    m_volumeProcess.start("wpctl", args);
}
