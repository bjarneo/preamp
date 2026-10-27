#include "plugins.h"

#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStringList>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <dlfcn.h>
#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/port-props/port-props.h>
#include <lv2/urid/urid.h>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace plugins {

namespace {

// ---------------------------------------------------------------- LADSPA ABI
//
// ladspa.h is not always installed, and the ABI has not changed since 2002, so
// the few declarations the host needs live here.

using LadspaData = float;
using LadspaHandle = void *;

struct LadspaRangeHint {
    int hints;
    LadspaData lower;
    LadspaData upper;
};

struct LadspaDescriptor {
    unsigned long uniqueId;
    const char *label;
    int properties;
    const char *name;
    const char *maker;
    const char *copyright;
    unsigned long portCount;
    const int *portDescriptors;
    const char *const *portNames;
    const LadspaRangeHint *portRangeHints;
    void *implementationData;
    LadspaHandle (*instantiate)(const LadspaDescriptor *, unsigned long);
    void (*connectPort)(LadspaHandle, unsigned long, LadspaData *);
    void (*activate)(LadspaHandle);
    void (*run)(LadspaHandle, unsigned long);
    void (*runAdding)(LadspaHandle, unsigned long);
    void (*setRunAddingGain)(LadspaHandle, LadspaData);
    void (*deactivate)(LadspaHandle);
    void (*cleanup)(LadspaHandle);
};

using LadspaDescriptorFunction = const LadspaDescriptor *(*)(unsigned long);

enum : int {
    PortInput = 0x1,
    PortOutput = 0x2,
    PortControl = 0x4,
    PortAudio = 0x8,
};

enum : int {
    HintBelow = 0x1,
    HintAbove = 0x2,
    HintToggled = 0x4,
    HintSampleRate = 0x8,
    HintLogarithmic = 0x10,
    HintInteger = 0x20,
    HintDefaultMask = 0x3C0,
    HintDefaultMinimum = 0x40,
    HintDefaultLow = 0x80,
    HintDefaultMiddle = 0xC0,
    HintDefaultHigh = 0x100,
    HintDefaultMaximum = 0x140,
    HintDefault0 = 0x200,
    HintDefault1 = 0x240,
    HintDefault100 = 0x280,
    HintDefault440 = 0x2C0,
};

QStringList ladspaDirectories() {
    const QString env = qEnvironmentVariable("LADSPA_PATH");
    if (!env.isEmpty())
        return env.split(':', Qt::SkipEmptyParts);
    return {"/usr/lib/ladspa", "/usr/local/lib/ladspa", "/usr/lib64/ladspa", QDir::homePath() + "/.ladspa"};
}

// Libraries stay loaded for the life of the process, so descriptors found by
// the scan stay valid for instantiation later.
const LadspaDescriptor *ladspaDescriptor(const QString &file, const QString &label) {
    static std::mutex mutex;
    static std::unordered_map<std::string, void *> libraries;
    std::lock_guard lock(mutex);
    void *&lib = libraries[file.toStdString()];
    if (!lib)
        lib = dlopen(file.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
    if (!lib)
        return nullptr;
    auto fn = reinterpret_cast<LadspaDescriptorFunction>(dlsym(lib, "ladspa_descriptor"));
    if (!fn)
        return nullptr;
    for (unsigned long i = 0;; ++i) {
        const LadspaDescriptor *d = fn(i);
        if (!d)
            return nullptr;
        if (label.isEmpty() || QString::fromUtf8(d->label) == label)
            return d;
    }
}

float ladspaDefault(const LadspaRangeHint &hint, double rate) {
    float lo = hint.lower, hi = hint.upper;
    if (hint.hints & HintSampleRate) {
        lo *= rate;
        hi *= rate;
    }
    const bool log = (hint.hints & HintLogarithmic) && lo > 0 && hi > 0;
    auto mix = [&](float a) {
        return log ? std::exp(std::log(lo) * (1 - a) + std::log(hi) * a) : lo * (1 - a) + hi * a;
    };
    switch (hint.hints & HintDefaultMask) {
    case HintDefaultMinimum: return lo;
    case HintDefaultLow: return mix(.25f);
    case HintDefaultMiddle: return mix(.5f);
    case HintDefaultHigh: return mix(.75f);
    case HintDefaultMaximum: return hi;
    case HintDefault0: return 0;
    case HintDefault1: return 1;
    case HintDefault100: return 100;
    case HintDefault440: return 440;
    default:
        if ((hint.hints & HintBelow) && (hint.hints & HintAbove))
            return lo <= 0 && hi >= 0 ? 0 : lo;
        if (hint.hints & HintBelow)
            return std::max(lo, 0.f);
        if (hint.hints & HintAbove)
            return std::min(hi, 0.f);
        return 0;
    }
}

// LADSPA ports have names only. "Attack time (ms)" becomes "attack_time_ms".
QString ladspaSymbol(const char *name) {
    QString symbol;
    for (const QChar c : QString::fromUtf8(name).toLower()) {
        if (c.isLetterOrNumber())
            symbol += c;
        else if (!symbol.isEmpty() && !symbol.endsWith('_'))
            symbol += '_';
    }
    while (symbol.endsWith('_'))
        symbol.chop(1);
    return symbol;
}

QVector<Param> ladspaParams(const LadspaDescriptor *d, double rate) {
    QVector<Param> params;
    for (unsigned long p = 0; p < d->portCount; ++p) {
        const int pd = d->portDescriptors[p];
        if (!(pd & PortControl) || !(pd & PortInput))
            continue;
        const LadspaRangeHint &hint = d->portRangeHints[p];
        const float scale = (hint.hints & HintSampleRate) ? float(rate) : 1.f;
        Param param;
        param.symbol = ladspaSymbol(d->portNames[p]);
        param.name = QString::fromUtf8(d->portNames[p]);
        param.port = uint32_t(p);
        param.toggled = hint.hints & HintToggled;
        param.integer = hint.hints & HintInteger;
        param.logarithmic = hint.hints & HintLogarithmic;
        param.min = param.toggled ? 0 : (hint.hints & HintBelow) ? hint.lower * scale : -FLT_MAX;
        param.max = param.toggled ? 1 : (hint.hints & HintAbove) ? hint.upper * scale : FLT_MAX;
        param.def = clamp(param, ladspaDefault(hint, rate));
        params << param;
    }
    return params;
}

class LadspaInstance : public Instance {
public:
    LadspaInstance(const LadspaDescriptor *d, double rate, int copies, const Values &values)
        : Instance(d->portCount), m_descriptor(d), m_controls(d->portCount, 0.f), m_scratch(MaxBlock, 0.f),
          m_silence(MaxBlock, 0.f) {
        for (unsigned long p = 0; p < d->portCount; ++p) {
            const int pd = d->portDescriptors[p];
            if (pd & PortAudio) {
                if (pd & PortInput)
                    m_audioIn.push_back(p);
                else
                    m_audioOut.push_back(p);
            }
        }
        for (const Param &param : ladspaParams(d, rate)) {
            m_controls[param.port] = values.contains(param.symbol) ? clamp(param, values[param.symbol]) : param.def;
            set(param.port, m_controls[param.port]);
            m_controlIn.push_back(param.port);
        }
        for (int i = 0; i < copies; ++i) {
            LadspaHandle h = d->instantiate(d, static_cast<unsigned long>(rate));
            if (!h)
                continue;
            for (unsigned long p = 0; p < d->portCount; ++p)
                if (d->portDescriptors[p] & PortControl)
                    d->connectPort(h, p, &m_controls[p]);
            if (d->activate)
                d->activate(h);
            m_handles.push_back(h);
        }
    }

    ~LadspaInstance() override {
        for (LadspaHandle h : m_handles) {
            if (m_descriptor->deactivate)
                m_descriptor->deactivate(h);
            m_descriptor->cleanup(h);
        }
    }

    bool ok() const { return !m_handles.empty() && !m_audioIn.empty() && !m_audioOut.empty(); }

    void run(const float *const in[2], float *const out[2], uint32_t frames) override {
        for (unsigned long p : m_controlIn)
            m_controls[p] = target(uint32_t(p));
        const bool stereo = m_audioIn.size() >= 2 && m_audioOut.size() >= 2;
        for (size_t k = 0; k < m_handles.size(); ++k) {
            LadspaHandle h = m_handles[k];
            for (size_t i = 0; i < m_audioIn.size(); ++i) {
                const int ch = stereo ? int(i) : int(k);
                const float *src = i < 2 && ch < 2 ? in[ch] : m_silence.data();
                m_descriptor->connectPort(h, m_audioIn[i], const_cast<float *>(src));
            }
            for (size_t i = 0; i < m_audioOut.size(); ++i) {
                const int ch = stereo ? int(i) : int(k);
                m_descriptor->connectPort(h, m_audioOut[i], i < 2 && ch < 2 ? out[ch] : m_scratch.data());
            }
            m_descriptor->run(h, frames);
        }
    }

private:
    const LadspaDescriptor *m_descriptor;
    std::vector<LadspaHandle> m_handles;
    std::vector<unsigned long> m_audioIn, m_audioOut, m_controlIn;
    std::vector<float> m_controls, m_scratch, m_silence;
};

// ------------------------------------------------------------------- LV2

class UridMap {
public:
    static UridMap &shared() {
        static UridMap map;
        return map;
    }

    LV2_URID map(const char *uri) {
        std::lock_guard lock(m_mutex);
        auto it = m_ids.find(uri);
        if (it != m_ids.end())
            return it->second;
        m_uris.emplace_back(uri);
        const LV2_URID id = LV2_URID(m_uris.size());
        m_ids.emplace(uri, id);
        return id;
    }

    const char *unmap(LV2_URID id) {
        std::lock_guard lock(m_mutex);
        return id > 0 && id <= m_uris.size() ? m_uris[id - 1].c_str() : nullptr;
    }

    LV2_URID_Map mapFeature{this, [](LV2_URID_Map_Handle h, const char *uri) {
                                return static_cast<UridMap *>(h)->map(uri);
                            }};
    LV2_URID_Unmap unmapFeature{this, [](LV2_URID_Unmap_Handle h, LV2_URID id) {
                                    return static_cast<UridMap *>(h)->unmap(id);
                                }};

private:
    std::mutex m_mutex;
    std::unordered_map<std::string, LV2_URID> m_ids;
    std::deque<std::string> m_uris;
};

struct Lv2World {
    LilvWorld *world = nullptr;
    LilvNode *audioPort = nullptr, *controlPort = nullptr, *atomPort = nullptr, *cvPort = nullptr;
    LilvNode *inputPort = nullptr, *outputPort = nullptr;
    LilvNode *sideChain = nullptr, *optional = nullptr;
    LilvNode *integer = nullptr, *toggled = nullptr, *logarithmic = nullptr, *notOnGui = nullptr;
};

Lv2World &lv2() {
    static Lv2World w = [] {
        Lv2World w;
        w.world = lilv_world_new();
        lilv_world_load_all(w.world);
        w.audioPort = lilv_new_uri(w.world, LV2_CORE__AudioPort);
        w.controlPort = lilv_new_uri(w.world, LV2_CORE__ControlPort);
        w.atomPort = lilv_new_uri(w.world, LV2_ATOM__AtomPort);
        w.cvPort = lilv_new_uri(w.world, LV2_CORE__CVPort);
        w.inputPort = lilv_new_uri(w.world, LV2_CORE__InputPort);
        w.outputPort = lilv_new_uri(w.world, LV2_CORE__OutputPort);
        w.sideChain = lilv_new_uri(w.world, LV2_CORE_PREFIX "isSideChain");
        w.optional = lilv_new_uri(w.world, LV2_CORE__connectionOptional);
        w.integer = lilv_new_uri(w.world, LV2_CORE__integer);
        w.toggled = lilv_new_uri(w.world, LV2_CORE__toggled);
        w.logarithmic = lilv_new_uri(w.world, LV2_PORT_PROPS__logarithmic);
        w.notOnGui = lilv_new_uri(w.world, LV2_PORT_PROPS__notOnGUI);
        return w;
    }();
    return w;
}

// The main audio ports: side chains and optional inputs are fed silence
// instead, so they do not count toward whether a plugin fits the chain.
void countAudio(const LilvPlugin *plugin, int *in, int *out) {
    Lv2World &w = lv2();
    *in = *out = 0;
    const uint32_t n = lilv_plugin_get_num_ports(plugin);
    for (uint32_t i = 0; i < n; ++i) {
        const LilvPort *port = lilv_plugin_get_port_by_index(plugin, i);
        if (!lilv_port_is_a(plugin, port, w.audioPort))
            continue;
        if (lilv_port_has_property(plugin, port, w.sideChain) || lilv_port_has_property(plugin, port, w.optional))
            continue;
        if (lilv_port_is_a(plugin, port, w.inputPort))
            ++*in;
        else
            ++*out;
    }
}

// Control inputs that the plugin marks as not for people, such as a host's
// latency or free-wheeling switch, stay at their default and are left out.
QVector<Param> lv2Params(const LilvPlugin *plugin) {
    Lv2World &w = lv2();
    const uint32_t n = lilv_plugin_get_num_ports(plugin);
    std::vector<float> mins(n), maxs(n), defs(n);
    lilv_plugin_get_port_ranges_float(plugin, mins.data(), maxs.data(), defs.data());
    QVector<Param> params;
    for (uint32_t i = 0; i < n; ++i) {
        const LilvPort *port = lilv_plugin_get_port_by_index(plugin, i);
        if (!lilv_port_is_a(plugin, port, w.controlPort) || !lilv_port_is_a(plugin, port, w.inputPort) ||
            lilv_port_has_property(plugin, port, w.notOnGui))
            continue;
        Param param;
        param.symbol = QString::fromUtf8(lilv_node_as_string(lilv_port_get_symbol(plugin, port)));
        LilvNode *name = lilv_port_get_name(plugin, port);
        param.name = name ? QString::fromUtf8(lilv_node_as_string(name)) : param.symbol;
        lilv_node_free(name);
        param.port = i;
        param.integer = lilv_port_has_property(plugin, port, w.integer);
        param.toggled = lilv_port_has_property(plugin, port, w.toggled);
        param.logarithmic = lilv_port_has_property(plugin, port, w.logarithmic);
        param.min = param.toggled ? 0 : std::isnan(mins[i]) ? -FLT_MAX : mins[i];
        param.max = param.toggled ? 1 : std::isnan(maxs[i]) ? FLT_MAX : maxs[i];
        param.def = clamp(param, std::isnan(defs[i]) ? (std::isnan(mins[i]) ? 0.f : mins[i]) : defs[i]);
        if (LilvScalePoints *points = lilv_port_get_scale_points(plugin, port)) {
            LILV_FOREACH (scale_points, it, points) {
                const LilvScalePoint *point = lilv_scale_points_get(points, it);
                param.options.append({lilv_node_as_float(lilv_scale_point_get_value(point)),
                                      QString::fromUtf8(lilv_node_as_string(lilv_scale_point_get_label(point)))});
            }
            lilv_scale_points_free(points);
            std::sort(param.options.begin(), param.options.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        }
        params << param;
    }
    return params;
}

class Lv2Instance : public Instance {
public:
    Lv2Instance(const LilvPlugin *plugin, double rate, const Values &values, QString *error)
        : Instance(lilv_plugin_get_num_ports(plugin)), m_silence(MaxBlock, 0.f), m_scratch(MaxBlock, 0.f) {
        Lv2World &w = lv2();
        UridMap &urids = UridMap::shared();
        m_sequenceType = urids.map(LV2_ATOM__Sequence);
        m_chunkType = urids.map(LV2_ATOM__Chunk);

        m_rate = float(rate);
        m_options = {
            {LV2_OPTIONS_INSTANCE, 0, urids.map(LV2_PARAMETERS__sampleRate), sizeof(float), urids.map(LV2_ATOM__Float), &m_rate},
            {LV2_OPTIONS_INSTANCE, 0, urids.map(LV2_BUF_SIZE__minBlockLength), sizeof(int32_t), urids.map(LV2_ATOM__Int), &m_minBlock},
            {LV2_OPTIONS_INSTANCE, 0, urids.map(LV2_BUF_SIZE__maxBlockLength), sizeof(int32_t), urids.map(LV2_ATOM__Int), &m_maxBlock},
            {LV2_OPTIONS_INSTANCE, 0, urids.map(LV2_BUF_SIZE__nominalBlockLength), sizeof(int32_t), urids.map(LV2_ATOM__Int), &m_nominalBlock},
            {LV2_OPTIONS_INSTANCE, 0, urids.map(LV2_BUF_SIZE__sequenceSize), sizeof(int32_t), urids.map(LV2_ATOM__Int), &m_sequenceSize},
            {LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr},
        };
        m_features = {
            {LV2_URID__map, &urids.mapFeature},
            {LV2_URID__unmap, &urids.unmapFeature},
            {LV2_OPTIONS__options, m_options.data()},
            {LV2_BUF_SIZE__boundedBlockLength, nullptr},
        };
        for (auto &f : m_features)
            m_featureList.push_back(&f);
        m_featureList.push_back(nullptr);

        // Refuse plugins that need something this host does not provide,
        // rather than letting them fail half way.
        LilvNodes *required = lilv_plugin_get_required_features(plugin);
        LILV_FOREACH (nodes, it, required) {
            const char *uri = lilv_node_as_uri(lilv_nodes_get(required, it));
            bool known = false;
            for (const auto &f : m_features)
                known |= std::string(f.URI) == uri;
            if (!known) {
                if (error)
                    *error = QStringLiteral("needs %1").arg(QString::fromUtf8(uri).section('#', -1).section('/', -1));
                lilv_nodes_free(required);
                return;
            }
        }
        lilv_nodes_free(required);

        const uint32_t n = lilv_plugin_get_num_ports(plugin);
        std::vector<float> mins(n), maxs(n), defs(n);
        lilv_plugin_get_port_ranges_float(plugin, mins.data(), maxs.data(), defs.data());
        m_ports.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            const LilvPort *port = lilv_plugin_get_port_by_index(plugin, i);
            const bool input = lilv_port_is_a(plugin, port, w.inputPort);
            Port &p = m_ports[i];
            if (lilv_port_is_a(plugin, port, w.audioPort)) {
                const bool extra = lilv_port_has_property(plugin, port, w.sideChain) || lilv_port_has_property(plugin, port, w.optional);
                p.role = input ? (extra ? Role::ExtraIn : Role::AudioIn) : (extra ? Role::ExtraOut : Role::AudioOut);
                if (p.role == Role::AudioIn)
                    m_audioIn.push_back(i);
                if (p.role == Role::AudioOut)
                    m_audioOut.push_back(i);
            } else if (lilv_port_is_a(plugin, port, w.controlPort)) {
                p.role = input ? Role::ControlIn : Role::ControlOut;
                p.value = std::isnan(defs[i]) ? (std::isnan(mins[i]) ? 0.f : mins[i]) : defs[i];
            } else if (lilv_port_is_a(plugin, port, w.atomPort)) {
                p.role = input ? Role::AtomIn : Role::AtomOut;
            } else {
                p.role = Role::Other; // CV and anything unknown: fed silence
            }
        }
        for (const Param &param : lv2Params(plugin))
            if (values.contains(param.symbol))
                m_ports[param.port].value = clamp(param, values[param.symbol]);
        for (uint32_t i = 0; i < n; ++i) {
            if (m_ports[i].role != Role::ControlIn)
                continue;
            set(i, m_ports[i].value);
            m_controlIn.push_back(i);
        }

        const bool stereo = m_audioIn.size() == 2 && m_audioOut.size() == 2;
        const int copies = stereo ? 1 : 2;
        for (int k = 0; k < copies; ++k) {
            LilvInstance *inst = lilv_plugin_instantiate(plugin, rate, m_featureList.data());
            if (!inst) {
                if (error)
                    *error = QStringLiteral("could not start");
                return;
            }
            Copy copy;
            copy.instance = inst;
            copy.controls.resize(n);
            copy.atoms.resize(n);
            for (uint32_t i = 0; i < n; ++i) {
                Port &p = m_ports[i];
                switch (p.role) {
                case Role::ControlIn:
                case Role::ControlOut:
                    copy.controls[i] = p.value;
                    lilv_instance_connect_port(inst, i, &copy.controls[i]);
                    break;
                case Role::AtomIn:
                case Role::AtomOut: {
                    copy.atoms[i].assign(m_sequenceSize, 0);
                    auto *seq = reinterpret_cast<LV2_Atom_Sequence *>(copy.atoms[i].data());
                    seq->atom.type = m_sequenceType;
                    seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
                    lilv_instance_connect_port(inst, i, seq);
                    break;
                }
                case Role::ExtraIn:
                case Role::Other:
                    lilv_instance_connect_port(inst, i, m_silence.data());
                    break;
                case Role::ExtraOut:
                    lilv_instance_connect_port(inst, i, m_scratch.data());
                    break;
                default:
                    break;
                }
            }
            lilv_instance_activate(inst);
            m_copies.push_back(std::move(copy));
        }
    }

    ~Lv2Instance() override {
        for (Copy &c : m_copies) {
            lilv_instance_deactivate(c.instance);
            lilv_instance_free(c.instance);
        }
    }

    bool ok() const { return !m_copies.empty() && !m_audioIn.empty() && !m_audioOut.empty(); }

    void run(const float *const in[2], float *const out[2], uint32_t frames) override {
        const bool stereo = m_copies.size() == 1;
        for (size_t k = 0; k < m_copies.size(); ++k) {
            Copy &c = m_copies[k];
            for (uint32_t i : m_controlIn)
                c.controls[i] = target(i);
            for (size_t i = 0; i < m_audioIn.size(); ++i) {
                const size_t ch = stereo ? i : k;
                lilv_instance_connect_port(c.instance, m_audioIn[i], const_cast<float *>(ch < 2 ? in[ch] : m_silence.data()));
            }
            for (size_t i = 0; i < m_audioOut.size(); ++i) {
                const size_t ch = stereo ? i : k;
                lilv_instance_connect_port(c.instance, m_audioOut[i], ch < 2 ? out[ch] : m_scratch.data());
            }
            // An output sequence is handed over empty with its full capacity.
            // An input sequence stays empty.
            for (size_t i = 0; i < m_ports.size(); ++i) {
                if (m_ports[i].role != Role::AtomOut)
                    continue;
                auto *atom = reinterpret_cast<LV2_Atom *>(c.atoms[i].data());
                atom->type = m_chunkType;
                atom->size = uint32_t(c.atoms[i].size() - sizeof(LV2_Atom));
            }
            lilv_instance_run(c.instance, frames);
        }
    }

private:
    enum class Role { AudioIn, AudioOut, ExtraIn, ExtraOut, ControlIn, ControlOut, AtomIn, AtomOut, Other };
    struct Port {
        Role role = Role::Other;
        float value = 0;
    };
    struct Copy {
        LilvInstance *instance = nullptr;
        std::vector<float> controls;
        std::vector<std::vector<uint8_t>> atoms;
    };

    float m_rate = 48000;
    int32_t m_minBlock = 1, m_maxBlock = int32_t(MaxBlock), m_nominalBlock = 1024, m_sequenceSize = 8192;
    LV2_URID m_sequenceType = 0, m_chunkType = 0;
    std::vector<LV2_Options_Option> m_options;
    std::vector<LV2_Feature> m_features;
    std::vector<const LV2_Feature *> m_featureList;
    std::vector<Port> m_ports;
    std::vector<uint32_t> m_audioIn, m_audioOut, m_controlIn;
    std::vector<Copy> m_copies;
    std::vector<float> m_silence, m_scratch;
};

} // namespace

float clamp(const Param &param, float value) {
    if (std::isnan(value))
        return param.def;
    value = std::clamp(value, param.min, param.max);
    if (param.toggled)
        return value > (param.min + param.max) / 2 ? param.max : param.min;
    return param.integer ? std::round(value) : value;
}

Catalog &Catalog::shared() {
    static Catalog catalog;
    return catalog;
}

bool Catalog::fitsStereo(const Info &info) {
    return (info.audioIn == 2 && info.audioOut == 2) || (info.audioIn == 1 && info.audioOut == 1);
}

void Catalog::scan() {
    if (m_scanned)
        return;
    m_scanned = true;

    Lv2World &w = lv2();
    const LilvPlugins *all = lilv_world_get_all_plugins(w.world);
    LILV_FOREACH (plugins, it, all) {
        const LilvPlugin *plugin = lilv_plugins_get(all, it);
        Info info;
        info.kind = "lv2";
        info.id = "lv2:" + QString::fromUtf8(lilv_node_as_uri(lilv_plugin_get_uri(plugin)));
        LilvNode *name = lilv_plugin_get_name(plugin);
        info.name = name ? QString::fromUtf8(lilv_node_as_string(name)) : info.id.mid(4);
        lilv_node_free(name);
        countAudio(plugin, &info.audioIn, &info.audioOut);
        m_list << info;
    }

    for (const QString &dir : ladspaDirectories()) {
        const auto files = QDir(dir).entryInfoList({"*.so"}, QDir::Files);
        for (const QFileInfo &file : files) {
            void *lib = dlopen(file.absoluteFilePath().toLocal8Bit().constData(), RTLD_LAZY | RTLD_LOCAL);
            if (!lib)
                continue;
            auto fn = reinterpret_cast<LadspaDescriptorFunction>(dlsym(lib, "ladspa_descriptor"));
            for (unsigned long i = 0; fn; ++i) {
                const LadspaDescriptor *d = fn(i);
                if (!d)
                    break;
                Info info;
                info.kind = "ladspa";
                info.id = QStringLiteral("ladspa:%1:%2").arg(file.absoluteFilePath(), QString::fromUtf8(d->label));
                info.name = QString::fromUtf8(d->name);
                for (unsigned long p = 0; p < d->portCount; ++p) {
                    const int pd = d->portDescriptors[p];
                    if (pd & PortAudio)
                        ++((pd & PortInput) ? info.audioIn : info.audioOut);
                }
                m_list << info;
            }
            dlclose(lib);
        }
    }

    std::sort(m_list.begin(), m_list.end(), [](const Info &a, const Info &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
}

QVector<Info> Catalog::list() {
    QMutexLocker lock(&m_mutex);
    scan();
    return m_list;
}

bool Catalog::find(const QString &id, Info *info) {
    QMutexLocker lock(&m_mutex);
    scan();
    for (const Info &i : std::as_const(m_list)) {
        if (i.id == id) {
            if (info)
                *info = i;
            return true;
        }
    }
    return false;
}

QVector<Param> Catalog::params(const QString &id, double sampleRate) {
    QMutexLocker lock(&m_mutex);
    scan();
    if (id.startsWith("lv2:")) {
        Lv2World &w = lv2();
        LilvNode *uri = lilv_new_uri(w.world, id.mid(4).toUtf8().constData());
        const LilvPlugin *plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(w.world), uri);
        lilv_node_free(uri);
        return plugin ? lv2Params(plugin) : QVector<Param>();
    }
    if (id.startsWith("ladspa:")) {
        const QString rest = id.mid(7);
        const int split = rest.lastIndexOf(':');
        const LadspaDescriptor *d = ladspaDescriptor(rest.left(split), rest.mid(split + 1));
        return d ? ladspaParams(d, sampleRate) : QVector<Param>();
    }
    return {};
}

std::unique_ptr<Instance> Catalog::instantiate(const QString &id, double sampleRate, const Values &values, QString *error) {
    QMutexLocker lock(&m_mutex);
    scan();
    if (id.startsWith("lv2:")) {
        Lv2World &w = lv2();
        LilvNode *uri = lilv_new_uri(w.world, id.mid(4).toUtf8().constData());
        const LilvPlugin *plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(w.world), uri);
        lilv_node_free(uri);
        if (!plugin) {
            if (error)
                *error = QStringLiteral("not installed");
            return nullptr;
        }
        int in = 0, out = 0;
        countAudio(plugin, &in, &out);
        if (!fitsStereo({id, {}, "lv2", in, out})) {
            if (error)
                *error = QStringLiteral("not stereo");
            return nullptr;
        }
        auto instance = std::make_unique<Lv2Instance>(plugin, sampleRate, values, error);
        return instance->ok() ? std::move(instance) : nullptr;
    }
    if (id.startsWith("ladspa:")) {
        const QString rest = id.mid(7);
        const int split = rest.lastIndexOf(':');
        const LadspaDescriptor *d = ladspaDescriptor(rest.left(split), rest.mid(split + 1));
        if (!d) {
            if (error)
                *error = QStringLiteral("not installed");
            return nullptr;
        }
        int in = 0, out = 0;
        for (unsigned long p = 0; p < d->portCount; ++p)
            if (d->portDescriptors[p] & PortAudio)
                ++((d->portDescriptors[p] & PortInput) ? in : out);
        if (!fitsStereo({id, {}, "ladspa", in, out})) {
            if (error)
                *error = QStringLiteral("not stereo");
            return nullptr;
        }
        auto instance = std::make_unique<LadspaInstance>(d, sampleRate, in == 2 ? 1 : 2, values);
        if (!instance->ok()) {
            if (error)
                *error = QStringLiteral("could not start");
            return nullptr;
        }
        return instance;
    }
    if (error)
        *error = QStringLiteral("unknown plugin");
    return nullptr;
}

} // namespace plugins
