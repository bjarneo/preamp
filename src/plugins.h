#pragma once

// Hosts the LV2 and LADSPA effects that run after the equalizer. The catalog
// scans what is installed. An instance runs one effect on a stereo block from
// the audio thread.

#include <QMutex>
#include <QString>
#include <QVector>
#include <cstdint>
#include <memory>

namespace plugins {

constexpr uint32_t MaxBlock = 8192;

struct Info {
    QString id;   // "lv2:<uri>" or "ladspa:<file>:<label>"
    QString name; // what the plugin calls itself
    QString kind; // "lv2" or "ladspa"
    int audioIn = 0;
    int audioOut = 0;
};

// Runs from the audio thread. `in` and `out` hold two channels of `frames`
// samples each, frames <= MaxBlock, and never alias.
class Instance {
public:
    virtual ~Instance() = default;
    virtual void run(const float *const in[2], float *const out[2], uint32_t frames) = 0;
};

class Catalog {
public:
    static Catalog &shared();

    // Scans on first use. Later calls return the same list.
    QVector<Info> list();
    bool find(const QString &id, Info *info);
    std::unique_ptr<Instance> instantiate(const QString &id, double sampleRate, QString *error);

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
