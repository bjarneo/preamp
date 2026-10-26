#pragma once

// The signal chain that runs on PipeWire's audio thread: preamp and ten
// biquads per channel, the plugin chain, stereo width, a room, and balance.
//
// The main thread never touches what the audio thread is using. It builds a
// new Params or Chain and posts it. The audio thread picks it up at the start
// of a block and hands back the one it replaced. The main thread frees that
// one in collect().

#include "eq.h"
#include "plugins.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace dsp {

enum class Room { Off, Small, Studio, Hall };

struct Params {
    double sampleRate = 48000;
    bool eq = true;
    float preamp = 1;              // linear gain in front of the biquads
    eq::Coefficients coeffs[2][eq::BandCount];
    float width = 1;               // 0 mono, 1 as is, 2 twice as wide
    Room room = Room::Off;
    float balanceLeft = 1, balanceRight = 1;
};

// Builds Params from the model. Bands are the left and right curves.
Params makeParams(double sampleRate, bool eqOn, const eq::Bands &left, const eq::Bands &right, double preampDb,
                  double width, Room room, double balance);

struct Chain {
    std::vector<std::unique_ptr<plugins::Instance>> instances;
};

// Single producer, single consumer: the audio thread writes the mono mix of
// what it sends to the speakers, the analyzer reads the newest samples.
class Ring {
public:
    static constexpr uint32_t Size = 1 << 15;
    void write(const float *left, const float *right, uint32_t frames);
    // Copies the newest `count` samples, oldest first. False until enough
    // audio has gone through.
    bool latest(float *dest, uint32_t count) const;
    uint64_t written() const { return m_write.load(std::memory_order_acquire); }

private:
    std::array<float, Size> m_data{};
    std::atomic<uint64_t> m_write{0};
};

class Reverb;

class Processor {
public:
    Processor();
    ~Processor();

    // Main thread.
    void post(const Params &params);
    void post(std::unique_ptr<Chain> chain);
    void collect();
    const Ring &ring() const { return m_ring; }

    // Audio thread.
    void process(const float *const in[2], float *const out[2], uint32_t frames);

private:
    void block(const float *const in[2], float *const out[2], uint32_t frames);

    template <typename T>
    struct Slot {
        std::atomic<T *> pending{nullptr};
        std::atomic<T *> retired{nullptr};
        T *active = nullptr;
        bool hasRetired = false;
    };
    template <typename T>
    static void postTo(Slot<T> &slot, T *next);
    template <typename T>
    static void adopt(Slot<T> &slot);
    template <typename T>
    static void collectFrom(Slot<T> &slot);

    Slot<Params> m_params;
    Slot<Chain> m_chain;

    // Biquad state, transposed direct form II, per channel and band.
    double m_z[2][eq::BandCount][2] = {};
    // Smoothed gains, so a slider never clicks.
    float m_preamp = 1, m_width = 1, m_balance[2] = {1, 1};
    double m_rate = 0;

    std::unique_ptr<Reverb> m_reverb;
    std::vector<float> m_a[2], m_b[2];
    Ring m_ring;
};

} // namespace dsp
