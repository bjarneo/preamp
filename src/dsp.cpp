#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace dsp {

// Freeverb by Jezar at Dreampoint, in the public domain. It has eight damped
// combs and four allpasses per side, tuned at 44.1 kHz and scaled to the
// graph rate.
class Reverb {
public:
    Reverb() {
        // Room for the longest delay at 192 kHz, so a rate change never
        // allocates on the audio thread.
        const int most = int(std::ceil((1617 + Spread) * 192000.0 / 44100.0)) + 1;
        for (int i = 0; i < 8; ++i) {
            m_combL[i].buf.assign(most, 0.f);
            m_combR[i].buf.assign(most, 0.f);
        }
        for (int i = 0; i < 4; ++i) {
            m_apL[i].buf.assign(most, 0.f);
            m_apR[i].buf.assign(most, 0.f);
        }
        setRate(48000);
        setRoom(Room::Studio);
    }

    void setRate(double rate) {
        const double scale = std::clamp(rate, 8000.0, 192000.0) / 44100.0;
        static const int combs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int passes[4] = {556, 441, 341, 225};
        for (int i = 0; i < 8; ++i) {
            m_combL[i].len = std::max(1, int(combs[i] * scale));
            m_combR[i].len = std::max(1, int((combs[i] + Spread) * scale));
        }
        for (int i = 0; i < 4; ++i) {
            m_apL[i].len = std::max(1, int(passes[i] * scale));
            m_apR[i].len = std::max(1, int((passes[i] + Spread) * scale));
        }
        clear();
    }

    void setRoom(Room room) {
        float size = .5f, damp = .5f;
        switch (room) {
        case Room::Small: size = .40f, damp = .60f, m_wet = .18f; break;
        case Room::Studio: size = .62f, damp = .45f, m_wet = .24f; break;
        case Room::Hall: size = .88f, damp = .25f, m_wet = .34f; break;
        case Room::Off: m_wet = 0; break;
        }
        m_feedback = size * .28f + .7f;
        m_damp1 = damp * .4f;
        m_damp2 = 1 - m_damp1;
    }

    void clear() {
        for (int i = 0; i < 8; ++i) {
            std::fill(m_combL[i].buf.begin(), m_combL[i].buf.end(), 0.f);
            std::fill(m_combR[i].buf.begin(), m_combR[i].buf.end(), 0.f);
            m_combL[i].idx = m_combR[i].idx = 0;
            m_combL[i].store = m_combR[i].store = 0;
        }
        for (int i = 0; i < 4; ++i) {
            std::fill(m_apL[i].buf.begin(), m_apL[i].buf.end(), 0.f);
            std::fill(m_apR[i].buf.begin(), m_apR[i].buf.end(), 0.f);
            m_apL[i].idx = m_apR[i].idx = 0;
        }
    }

    void process(float *left, float *right, uint32_t frames) {
        for (uint32_t n = 0; n < frames; ++n) {
            const float input = (left[n] + right[n]) * .015f;
            float outL = 0, outR = 0;
            for (int i = 0; i < 8; ++i) {
                outL += m_combL[i].process(input, m_feedback, m_damp1, m_damp2);
                outR += m_combR[i].process(input, m_feedback, m_damp1, m_damp2);
            }
            for (int i = 0; i < 4; ++i) {
                outL = m_apL[i].process(outL);
                outR = m_apR[i].process(outR);
            }
            left[n] += outL * m_wet;
            right[n] += outR * m_wet;
        }
    }

private:
    static constexpr int Spread = 23;

    struct Comb {
        std::vector<float> buf;
        int len = 1, idx = 0;
        float store = 0;
        float process(float in, float feedback, float damp1, float damp2) {
            const float out = buf[idx];
            store = out * damp2 + store * damp1;
            buf[idx] = in + store * feedback;
            if (++idx >= len)
                idx = 0;
            return out;
        }
    };
    struct Allpass {
        std::vector<float> buf;
        int len = 1, idx = 0;
        float process(float in) {
            const float held = buf[idx];
            buf[idx] = in + held * .5f;
            if (++idx >= len)
                idx = 0;
            return held - in;
        }
    };

    Comb m_combL[8], m_combR[8];
    Allpass m_apL[4], m_apR[4];
    float m_feedback = .84f, m_damp1 = .2f, m_damp2 = .8f, m_wet = 0;
};

Params makeParams(double sampleRate, bool eqOn, const eq::Bands &left, const eq::Bands &right, double preampDb,
                  double width, Room room, double balance) {
    Params p;
    p.sampleRate = sampleRate;
    p.eq = eqOn;
    p.preamp = float(std::pow(10.0, preampDb / 20));
    const eq::Bands *sides[2] = {&left, &right};
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < eq::BandCount && i < sides[ch]->size(); ++i)
            p.coeffs[ch][i] = eq::design((*sides[ch])[i], sampleRate);
    p.width = float(std::clamp(width, 0.0, 2.0));
    p.room = room;
    const double b = std::clamp(balance, -100.0, 100.0) / 100;
    p.balanceLeft = float(b > 0 ? 1 - b : 1);
    p.balanceRight = float(b < 0 ? 1 + b : 1);
    return p;
}

void Ring::write(const float *left, const float *right, uint32_t frames) {
    uint64_t w = m_write.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < frames; ++i)
        m_data[(w + i) & (Size - 1)] = (left[i] + right[i]) * .5f;
    m_write.store(w + frames, std::memory_order_release);
}

bool Ring::latest(float *dest, uint32_t count) const {
    const uint64_t w = m_write.load(std::memory_order_acquire);
    if (count > Size || w < count)
        return false;
    for (uint32_t i = 0; i < count; ++i)
        dest[i] = m_data[(w - count + i) & (Size - 1)];
    return true;
}

Processor::Processor() : m_reverb(std::make_unique<Reverb>()) {
    for (int ch = 0; ch < 2; ++ch) {
        m_a[ch].assign(plugins::MaxBlock, 0.f);
        m_b[ch].assign(plugins::MaxBlock, 0.f);
    }
}

Processor::~Processor() {
    for (auto *slot : {&m_params}) {
        delete slot->pending.exchange(nullptr);
        delete slot->retired.exchange(nullptr);
        delete slot->active;
    }
    delete m_chain.pending.exchange(nullptr);
    delete m_chain.retired.exchange(nullptr);
    delete m_chain.active;
}

template <typename T>
void Processor::postTo(Slot<T> &slot, T *next) {
    // Whatever was still pending never reached the audio thread.
    delete slot.pending.exchange(next, std::memory_order_acq_rel);
}

template <typename T>
void Processor::adopt(Slot<T> &slot) {
    // Take the new one only when the last one handed back has been freed, so
    // there is never more than one waiting for the main thread.
    if (slot.retired.load(std::memory_order_acquire))
        return;
    T *next = slot.pending.exchange(nullptr, std::memory_order_acq_rel);
    if (!next)
        return;
    slot.retired.store(slot.active, std::memory_order_release);
    slot.active = next;
}

template <typename T>
void Processor::collectFrom(Slot<T> &slot) {
    delete slot.retired.exchange(nullptr, std::memory_order_acq_rel);
}

void Processor::post(const Params &params) { postTo(m_params, new Params(params)); }
void Processor::post(std::unique_ptr<Chain> chain) { postTo(m_chain, chain.release()); }

void Processor::collect() {
    collectFrom(m_params);
    collectFrom(m_chain);
}

void Processor::process(const float *const in[2], float *const out[2], uint32_t frames) {
#if defined(__SSE__)
    // Reverb tails and biquads decay into denormals, which are slow on x86.
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
    adopt(m_params);
    adopt(m_chain);
    uint32_t done = 0;
    while (done < frames) {
        const uint32_t n = std::min(frames - done, plugins::MaxBlock);
        const float *src[2] = {in[0] + done, in[1] + done};
        float *dst[2] = {out[0] + done, out[1] + done};
        block(src, dst, n);
        done += n;
    }
}

void Processor::block(const float *const in[2], float *const out[2], uint32_t frames) {
    const Params *p = m_params.active;
    if (!p) {
        for (int ch = 0; ch < 2; ++ch)
            std::memcpy(out[ch], in[ch], frames * sizeof(float));
        m_ring.write(out[0], out[1], frames);
        return;
    }
    if (p->sampleRate != m_rate) {
        m_rate = p->sampleRate;
        m_reverb->setRate(m_rate);
        std::memset(m_z, 0, sizeof(m_z));
    }

    float *a[2] = {m_a[0].data(), m_a[1].data()};
    float *b[2] = {m_b[0].data(), m_b[1].data()};
    const float step = 1.f / float(frames);

    // Preamp, ramped across the block.
    const float preampTarget = p->eq ? p->preamp : 1.f;
    for (int ch = 0; ch < 2; ++ch)
        for (uint32_t i = 0; i < frames; ++i)
            a[ch][i] = in[ch][i] * (m_preamp + (preampTarget - m_preamp) * float(i + 1) * step);
    m_preamp = preampTarget;

    if (p->eq) {
        for (int ch = 0; ch < 2; ++ch) {
            for (int k = 0; k < eq::BandCount; ++k) {
                const eq::Coefficients &c = p->coeffs[ch][k];
                double z1 = m_z[ch][k][0], z2 = m_z[ch][k][1];
                if (c.b0 == 1 && c.b1 == 0 && c.b2 == 0 && c.a1 == 0 && c.a2 == 0 && z1 == 0 && z2 == 0)
                    continue;
                float *x = a[ch];
                for (uint32_t i = 0; i < frames; ++i) {
                    const double in0 = x[i];
                    const double y = c.b0 * in0 + z1;
                    z1 = c.b1 * in0 - c.a1 * y + z2;
                    z2 = c.b2 * in0 - c.a2 * y;
                    x[i] = float(y);
                }
                m_z[ch][k][0] = z1;
                m_z[ch][k][1] = z2;
            }
        }
    }

    if (Chain *chain = m_chain.active) {
        for (auto &instance : chain->instances) {
            instance->run(a, b, frames);
            std::swap(a[0], b[0]);
            std::swap(a[1], b[1]);
        }
    }

    // Width as mid and side. A width of 1 leaves the signal alone.
    if (p->width != 1.f || m_width != 1.f) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float w = m_width + (p->width - m_width) * float(i + 1) * step;
            const float mid = (a[0][i] + a[1][i]) * .5f;
            const float side = (a[0][i] - a[1][i]) * .5f * w;
            a[0][i] = mid + side;
            a[1][i] = mid - side;
        }
        m_width = p->width;
    }

    static thread_local Room room = Room::Off;
    if (p->room != room) {
        if (room == Room::Off)
            m_reverb->clear();
        room = p->room;
        m_reverb->setRoom(room);
    }
    if (room != Room::Off)
        m_reverb->process(a[0], a[1], frames);

    const float targets[2] = {p->balanceLeft, p->balanceRight};
    for (int ch = 0; ch < 2; ++ch) {
        for (uint32_t i = 0; i < frames; ++i)
            out[ch][i] = a[ch][i] * (m_balance[ch] + (targets[ch] - m_balance[ch]) * float(i + 1) * step);
        m_balance[ch] = targets[ch];
    }
    m_ring.write(out[0], out[1], frames);
}

} // namespace dsp
