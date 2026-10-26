#pragma once

// The equalizer model shared by the daemon, the window and the tests: ten
// bands, the RBJ biquads they become, and the presets that ship built in.

#include <QJsonArray>
#include <QString>
#include <QVector>

namespace eq {

enum class FilterType { LowShelf, Peak, HighShelf };

struct Band {
    double freq = 1000;
    double gain = 0;
    double q = 1.41;
    FilterType type = FilterType::Peak;

    bool operator==(const Band &other) const = default;
};

using Bands = QVector<Band>;

constexpr int BandCount = 10;
constexpr double MinFreq = 20, MaxFreq = 20000;
constexpr double MinGain = -12, MaxGain = 12;
constexpr double MinQ = 0.3, MaxQ = 10;
constexpr double DisplayRate = 48000;

// The ISO centres the ten bands start on, low shelf first and high shelf last.
extern const double IsoFreqs[BandCount];

// Normalized direct form coefficients, with a0 divided out.
struct Coefficients {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

Coefficients design(const Band &band, double sampleRate);
double response(const Band &band, double freq, double sampleRate = DisplayRate);
double response(const Bands &bands, double freq, double sampleRate = DisplayRate);

// The cut that keeps the loudest point of either channel's curve at 0 dB,
// rounded the way the window shows it: up to the next half decibel.
double autoPreamp(const Bands &left, const Bands &right);

// Bass, mid and treble: weighted averages over the bands, and the shift that
// moves one of them to a new value.
double macro(const Bands &bands, int which);
Bands withMacro(const Bands &bands, int which, double value);

Bands fromGains(const QVector<double> &gains);
Bands flat();
Bands normalized(Bands bands);

double clampFreq(double f);
double clampGain(double g);
double clampQ(double q);

QString typeName(FilterType type);
FilterType typeFromName(const QString &name);

QJsonArray toJson(const Bands &bands);
Bands fromJson(const QJsonArray &array);

struct Preset {
    QString name;
    QString kind; // "builtin", "autoeq" or "user"
    Bands left;
    Bands right;
};

// The everyday presets come first and keep keys 1 to 6. The others get a
// key only when they are among the presets used most recently.
QVector<Preset> builtins();
constexpr int EverydayPresets = 6;

// Parses AutoEQ's "ParametricEQ.txt" into ten bands sorted by frequency.
// Returns an empty list when the text holds no usable filter.
Bands parseAutoEq(const QString &text);

} // namespace eq
