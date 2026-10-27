#pragma once

// Hosts the LV2 and LADSPA effects that run after the equalizer. The catalog
// scans what is installed. An instance runs one effect on a stereo block from
// the audio thread.

#include <QHash>
#include <QMutex>
#include <QPair>
#include <QString>
#include <QVector>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace plugins {

constexpr uint32_t MaxBlock = 8192;

struct Info {
    QString id;   // "lv2:<uri>" or "ladspa:<file>:<label>"
    QString name; // what the plugin calls itself
    QString kind; // "lv2" or "ladspa"
    int audioIn = 0;
    int audioOut = 0;
};

// A control input of an effect: a knob, a switch or a list of named values.
struct Param {
    QString symbol; // the LV2 symbol, or the LADSPA port name in snake case
    QString name;   // what the plugin calls it
    uint32_t port = 0;
    float min = 0, max = 1, def = 0; // an unbounded side is -/+FLT_MAX
    bool integer = false, toggled = false, logarithmic = false;
    QVector<QPair<float, QString>> options; // the named values, in order
};

// Values by symbol. A control without one keeps its default.
using Values = QHash<QString, float>;

// Holds a value inside the range of the control, and on a whole number when
// the control counts or switches.
float clamp(const Param &param, float value);

// Runs from the audio thread. `in` and `out` hold two channels of `frames`
// samples each, frames <= MaxBlock, and never alias.
class Instance {
public:
    virtual ~Instance() = default;
    virtual void run(const float *const in[2], float *const out[2], uint32_t frames) = 0;

    // Main thread. The effect picks the value up at the start of the next
    // block, so it keeps its state and does not click.
    void set(uint32_t port, float value) {
        if (port < m_targets.size())
            m_targets[port].store(value, std::memory_order_relaxed);
    }

protected:
    // One target per port. Only control inputs read theirs.
    explicit Instance(size_t ports) : m_targets(ports) {}
    float target(uint32_t port) const { return m_targets[port].load(std::memory_order_relaxed); }

private:
    std::vector<std::atomic<float>> m_targets;
};

class Catalog {
public:
    static Catalog &shared();

    // Scans on first use. Later calls return the same list.
    QVector<Info> list();
    bool find(const QString &id, Info *info);
    // The control inputs, in port order. Empty for an unknown plugin.
    QVector<Param> params(const QString &id, double sampleRate);
    // Starts with `values` in place of the defaults.
    std::unique_ptr<Instance> instantiate(const QString &id, double sampleRate, const Values &values, QString *error);

    // A stereo plugin runs once. A mono plugin runs once per channel.
    // Anything else has no place in a stereo chain.
    static bool fitsStereo(const Info &info);

private:
    Catalog() = default;
    void scan();

    QMutex m_mutex;
    bool m_scanned = false;
    QVector<Info> m_list;
};

} // namespace plugins
