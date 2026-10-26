#include "analyzer.h"

#include <algorithm>
#include <cmath>

Analyzer::Analyzer() : m_window(Size), m_buffer(Size), m_twiddles(Size / 2), m_samples(Size) {
    for (int i = 0; i < Size; ++i)
        m_window[i] = float(0.5 - 0.5 * std::cos(2 * M_PI * i / (Size - 1)));
    for (int i = 0; i < Size / 2; ++i)
        m_twiddles[i] = std::polar(1.f, float(-2 * M_PI * i / Size));
}

QVector<float> Analyzer::compute(const dsp::Ring &ring, double sampleRate) {
    const uint64_t written = ring.written();
    if (written == m_lastWritten || !ring.latest(m_samples.data(), Size))
        return QVector<float>(Bands, Floor);
    m_lastWritten = written;
    return levels(m_samples.data(), sampleRate);
}

QVector<float> Analyzer::levels(const float *samples, double sampleRate) {
    if (sampleRate <= 0)
        sampleRate = 48000;
    for (int i = 0; i < Size; ++i)
        m_buffer[i] = {samples[i] * m_window[i], 0.f};

    // In-place radix-2 FFT.
    for (int i = 1, j = 0; i < Size; ++i) {
        int bit = Size >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(m_buffer[i], m_buffer[j]);
    }
    for (int len = 2; len <= Size; len <<= 1) {
        const int stride = Size / len;
        for (int i = 0; i < Size; i += len) {
            for (int k = 0; k < len / 2; ++k) {
                const std::complex<float> t = m_twiddles[k * stride] * m_buffer[i + k + len / 2];
                m_buffer[i + k + len / 2] = m_buffer[i + k] - t;
                m_buffer[i + k] += t;
            }
        }
    }

    // A full-scale sine through a Hann window peaks at Size / 4.
    const float scale = 4.f / Size;
    auto magnitude = [&](int bin) { return std::abs(m_buffer[std::clamp(bin, 0, Size / 2 - 1)]) * scale; };
    const double binHz = sampleRate / Size;
    const double logMin = std::log10(20.0), logMax = std::log10(20000.0);

    QVector<float> out(Bands);
    for (int b = 0; b < Bands; ++b) {
        const double lo = std::pow(10, logMin + double(b) / Bands * (logMax - logMin));
        const double hi = std::pow(10, logMin + double(b + 1) / Bands * (logMax - logMin));
        const int first = int(std::ceil(lo / binHz)), last = int(std::floor(hi / binHz));
        float peak = 0;
        if (last >= first) {
            for (int k = first; k <= last; ++k)
                peak = std::max(peak, magnitude(k));
        } else {
            // Narrower than a bin at the low end: read between the two bins
            // around the band's centre.
            const double centre = std::sqrt(lo * hi) / binHz;
            const int k = int(std::floor(centre));
            const float t = float(centre - k);
            peak = magnitude(k) * (1 - t) + magnitude(k + 1) * t;
        }
        out[b] = std::max(Floor, peak > 0 ? 20 * std::log10(peak) : Floor);
    }
    return out;
}
