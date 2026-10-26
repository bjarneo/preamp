#include "eq.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace eq {

const double IsoFreqs[BandCount] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

namespace {

const double LogMin = std::log10(MinFreq);
const double LogMax = std::log10(MaxFreq);

// How much each band counts toward bass, mid and treble.
const double MacroWeights[3][BandCount] = {
    {1, .85, .5, .15, 0, 0, 0, 0, 0, 0},
    {0, 0, .2, .6, 1, 1, .6, .2, 0, 0},
    {0, 0, 0, 0, 0, 0, .15, .5, .85, 1},
};

double roundTenth(double v) { return std::round(v * 10) / 10; }

} // namespace

double clampFreq(double f) { return std::clamp(f, MinFreq, MaxFreq); }
double clampGain(double g) { return std::clamp(g, MinGain, MaxGain); }
double clampQ(double q) { return std::clamp(q, MinQ, MaxQ); }

Coefficients design(const Band &band, double sampleRate) {
    Coefficients c;
    if (std::abs(band.gain) < 1e-3 || sampleRate <= 0)
        return c;
    // A centre at or past Nyquist has no stable design, so hold it just under.
    const double f0 = std::min(band.freq, sampleRate * 0.49);
    const double A = std::pow(10, band.gain / 40);
    const double w0 = 2 * M_PI * f0 / sampleRate;
    const double cs = std::cos(w0);
    const double alpha = std::sin(w0) / (2 * band.q);
    const double sA = 2 * std::sqrt(A) * alpha;
    double b0, b1, b2, a0, a1, a2;
    switch (band.type) {
    case FilterType::LowShelf:
        b0 = A * ((A + 1) - (A - 1) * cs + sA);
        b1 = 2 * A * ((A - 1) - (A + 1) * cs);
        b2 = A * ((A + 1) - (A - 1) * cs - sA);
        a0 = (A + 1) + (A - 1) * cs + sA;
        a1 = -2 * ((A - 1) + (A + 1) * cs);
        a2 = (A + 1) + (A - 1) * cs - sA;
        break;
    case FilterType::HighShelf:
        b0 = A * ((A + 1) + (A - 1) * cs + sA);
        b1 = -2 * A * ((A - 1) + (A + 1) * cs);
        b2 = A * ((A + 1) + (A - 1) * cs - sA);
        a0 = (A + 1) - (A - 1) * cs + sA;
        a1 = 2 * ((A - 1) - (A + 1) * cs);
        a2 = (A + 1) - (A - 1) * cs - sA;
        break;
    case FilterType::Peak:
    default:
        b0 = 1 + alpha * A;
        b1 = -2 * cs;
        b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        a1 = -2 * cs;
        a2 = 1 - alpha / A;
        break;
    }
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}

double response(const Band &band, double freq, double sampleRate) {
    if (std::abs(band.gain) < 1e-3)
        return 0;
    const Coefficients c = design(band, sampleRate);
    const double w = 2 * M_PI * freq / sampleRate;
    const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
    const double nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = -(c.b1 * s1 + c.b2 * s2);
    const double dr = 1 + c.a1 * c1 + c.a2 * c2, di = -(c.a1 * s1 + c.a2 * s2);
    return 10 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

double response(const Bands &bands, double freq, double sampleRate) {
    double sum = 0;
    for (const Band &band : bands)
        sum += response(band, freq, sampleRate);
    return sum;
}

double autoPreamp(const Bands &left, const Bands &right) {
    // A dense sweep plus every band centre, so a narrow peak between two sweep
    // points still counts.
    QVector<double> freqs;
    for (int i = 0; i <= 400; ++i)
        freqs << std::pow(10, LogMin + i / 400.0 * (LogMax - LogMin));
    for (const Band &b : left)
        freqs << b.freq;
    for (const Band &b : right)
        freqs << b.freq;
    double loudest = 0;
    for (double f : freqs)
        loudest = std::max({loudest, response(left, f), response(right, f)});
    const double cut = -std::ceil(loudest * 2) / 2;
    return cut == 0 ? 0 : cut;
}

double macro(const Bands &bands, int which) {
    if (which < 0 || which > 2 || bands.size() != BandCount)
        return 0;
    double sum = 0, weight = 0;
    for (int i = 0; i < BandCount; ++i) {
        sum += MacroWeights[which][i] * bands[i].gain;
        weight += MacroWeights[which][i];
    }
    return sum / weight;
}

Bands withMacro(const Bands &bands, int which, double value) {
    if (which < 0 || which > 2 || bands.size() != BandCount)
        return bands;
    // Shift each band by its weight, scaled so the weighted average lands
    // on the value asked for rather than short of it.
    double sum = 0, squares = 0;
    for (int i = 0; i < BandCount; ++i) {
        sum += MacroWeights[which][i];
        squares += MacroWeights[which][i] * MacroWeights[which][i];
    }
    const double delta = (value - macro(bands, which)) * sum / squares;
    Bands out = bands;
    for (int i = 0; i < BandCount; ++i)
        out[i].gain = clampGain(roundTenth(out[i].gain + delta * MacroWeights[which][i]));
    return out;
}

Bands fromGains(const QVector<double> &gains) {
    Bands bands(BandCount);
    for (int i = 0; i < BandCount; ++i) {
        bands[i].freq = IsoFreqs[i];
        bands[i].gain = i < gains.size() ? gains[i] : 0;
        bands[i].q = (i == 0 || i == BandCount - 1) ? 0.71 : 1.41;
        bands[i].type = i == 0 ? FilterType::LowShelf : i == BandCount - 1 ? FilterType::HighShelf : FilterType::Peak;
    }
    return bands;
}

Bands flat() { return fromGains({}); }

// Ten bands, each inside its limits. Anything else falls back to flat.
Bands normalized(Bands bands) {
    if (bands.size() != BandCount)
        return flat();
    for (Band &b : bands) {
        b.freq = clampFreq(std::isfinite(b.freq) ? b.freq : 1000);
        b.gain = clampGain(std::isfinite(b.gain) ? b.gain : 0);
        b.q = clampQ(std::isfinite(b.q) ? b.q : 1.41);
    }
    return bands;
}

QString typeName(FilterType type) {
    switch (type) {
    case FilterType::LowShelf: return "ls";
    case FilterType::HighShelf: return "hs";
    default: return "pk";
    }
}

FilterType typeFromName(const QString &name) {
    if (name == "ls")
        return FilterType::LowShelf;
    if (name == "hs")
        return FilterType::HighShelf;
    return FilterType::Peak;
}

QJsonArray toJson(const Bands &bands) {
    QJsonArray array;
    for (const Band &b : bands)
        array.append(QJsonObject{{"f", b.freq}, {"g", b.gain}, {"q", b.q}, {"t", typeName(b.type)}});
    return array;
}

Bands fromJson(const QJsonArray &array) {
    Bands bands;
    for (const auto &value : array) {
        const QJsonObject o = value.toObject();
        Band b;
        b.freq = o.value("f").toDouble(1000);
        b.gain = o.value("g").toDouble(0);
        b.q = o.value("q").toDouble(1.41);
        b.type = typeFromName(o.value("t").toString());
        bands << b;
    }
    return normalized(bands);
}

QVector<Preset> builtins() {
    auto preset = [](const QString &name, const QVector<double> &gains) {
        const Bands bands = fromGains(gains);
        return Preset{name, "builtin", bands, bands};
    };
    return {
        preset("Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
        preset("Bass boost", {6, 5, 3.5, 1.5, 0, 0, 0, 0, 0, 0}),
        preset("Vocal", {-2, -1.5, -1, 0, 1.5, 3, 3.5, 2, 0, -1}),
        preset("Loudness", {5, 4, 2, 0, -1, -1, 0, 2, 4, 4.5}),
        preset("Night", {-4, -3, -1.5, 0, 1, 1.5, 1.5, 0, -2, -4}),
        preset("Podcast", {-6, -4, -1, 1, 2, 2.5, 2, 0.5, -1, -3}),
        preset("Treble boost", {0, 0, 0, 0, 0, 0, 1, 2.5, 4, 5}),
        preset("Bass cut", {-6, -5, -3.5, -1.5, 0, 0, 0, 0, 0, 0}),
        preset("Warm", {2.5, 2.5, 2, 1, 0, -0.5, -1, -2, -3, -4}),
        preset("Rock", {4, 3, 1.5, 0, -1, -0.5, 1, 2.5, 3, 2.5}),
        preset("Movie", {4, 3.5, 2, 0.5, 0, 1, 2.5, 2, 1, 0}),
        preset("Gaming", {-2, -1, 0, -1, -1, 0.5, 2.5, 4, 3, 1}),
    };
}

Bands parseAutoEq(const QString &text) {
    static const QRegularExpression line(
        R"(Filter\s+\d+:\s+ON\s+(PK|PEQ|LSC|LS|LSQ|HSC|HS|HSQ)\s+Fc\s+([0-9.]+)\s*Hz\s+Gain\s+(-?[0-9.]+)\s*dB(?:\s+Q\s+([0-9.]+))?)",
        QRegularExpression::CaseInsensitiveOption);
    Bands found;
    auto it = line.globalMatch(text);
    while (it.hasNext() && found.size() < BandCount) {
        const auto m = it.next();
        const QString kind = m.captured(1).toUpper();
        Band b;
        b.type = kind.startsWith("LS") ? FilterType::LowShelf : kind.startsWith("HS") ? FilterType::HighShelf : FilterType::Peak;
        b.freq = clampFreq(m.captured(2).toDouble());
        b.gain = clampGain(m.captured(3).toDouble());
        const double q = m.captured(4).isEmpty() ? (b.type == FilterType::Peak ? 1.41 : 0.71) : m.captured(4).toDouble();
        b.q = clampQ(q);
        found << b;
    }
    if (found.isEmpty())
        return {};

    // Fewer than ten filters: fill the gaps with flat bands on the ISO centres
    // farthest from what the profile already uses.
    QVector<double> spare(IsoFreqs, IsoFreqs + BandCount);
    while (found.size() < BandCount && !spare.isEmpty()) {
        auto distance = [&](double f) {
            double nearest = 1e9;
            for (const Band &b : found)
                nearest = std::min(nearest, std::abs(std::log2(f / b.freq)));
            return nearest;
        };
        auto best = std::max_element(spare.begin(), spare.end(),
                                     [&](double a, double b) { return distance(a) < distance(b); });
        Band b;
        b.freq = *best;
        found << b;
        spare.erase(best);
    }
    std::sort(found.begin(), found.end(), [](const Band &a, const Band &b) { return a.freq < b.freq; });
    return found;
}

} // namespace eq
