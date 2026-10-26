#pragma once

// Turns the newest output samples into 64 log-spaced band levels between
// 20 Hz and 20 kHz, in dB relative to a full-scale sine.

#include "dsp.h"

#include <QVector>
#include <complex>
#include <vector>

class Analyzer {
public:
    static constexpr int Bands = 64;
    static constexpr int Size = 4096;
    static constexpr float Floor = -90;

    Analyzer();

    // Levels for the audio written since the last call. All bands sit at the
    // floor when nothing new played.
    QVector<float> compute(const dsp::Ring &ring, double sampleRate);

    // Exposed for the tests.
    QVector<float> levels(const float *samples, double sampleRate);

private:
    std::vector<float> m_window;
    std::vector<std::complex<float>> m_buffer, m_twiddles;
    std::vector<float> m_samples;
    uint64_t m_lastWritten = 0;
};
