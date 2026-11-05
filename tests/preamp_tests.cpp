#include <QDir>
#include <QFile>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <cfloat>
#include <cmath>

#include "analyzer.h"
#include "autoeq.h"
#include "brand.h"
#include "dsp.h"
#include "eq.h"
#include "graph.h"
#include "plugins.h"
#include "theme.h"
#include "translator.h"

namespace {

void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

std::vector<float> sine(double freq, double amplitude, int frames, double rate = 48000) {
    std::vector<float> out(frames);
    for (int i = 0; i < frames; ++i)
        out[i] = float(amplitude * std::sin(2 * M_PI * freq * i / rate));
    return out;
}

// Runs a mono signal through the processor on both channels, in quanta.
std::vector<float> run(dsp::Processor &p, const std::vector<float> &in, uint32_t quantum = 1024) {
    std::vector<float> outL(in.size()), outR(in.size());
    for (size_t at = 0; at < in.size(); at += quantum) {
        const uint32_t n = uint32_t(std::min<size_t>(quantum, in.size() - at));
        const float *src[2] = {in.data() + at, in.data() + at};
        float *dst[2] = {outL.data() + at, outR.data() + at};
        p.process(src, dst, n);
    }
    return outL;
}

double peakOf(const std::vector<float> &v, size_t from) {
    double peak = 0;
    for (size_t i = from; i < v.size(); ++i)
        peak = std::max(peak, double(std::abs(v[i])));
    return peak;
}

} // namespace

class PreampTests : public QObject {
    Q_OBJECT

private slots:
    void flatIsFlat() {
        const eq::Bands flat = eq::flat();
        QCOMPARE(flat.size(), eq::BandCount);
        for (double f : {20.0, 100.0, 1000.0, 10000.0, 20000.0})
            QVERIFY(std::abs(eq::response(flat, f)) < 1e-9);
        QCOMPARE(eq::autoPreamp(flat, flat), 0.0);
    }

    void peakReachesItsGainAtItsCentre() {
        eq::Band b{1000, 6, 1.41, eq::FilterType::Peak};
        QVERIFY(std::abs(eq::response(b, 1000) - 6) < 0.05);
        QVERIFY(std::abs(eq::response(b, 20)) < 0.1);
    }

    void shelvesReachTheirGainAtTheEdges() {
        eq::Band low{100, -8, 0.71, eq::FilterType::LowShelf};
        eq::Band high{8000, 5, 0.71, eq::FilterType::HighShelf};
        QVERIFY(std::abs(eq::response(low, 20) + 8) < 0.3);
        QVERIFY(std::abs(eq::response(high, 20000) - 5) < 0.4);
    }

    void preampCutsTheLoudestPointToHalfDecibels() {
        eq::Bands boost = eq::fromGains({6, 5, 3.5, 1.5, 0, 0, 0, 0, 0, 0});
        const double cut = eq::autoPreamp(boost, boost);
        QVERIFY(cut < 0);
        QCOMPARE(std::fmod(cut * 2, 1.0), 0.0);
        double loudest = 0;
        for (int i = 0; i <= 400; ++i)
            loudest = std::max(loudest, eq::response(boost, std::pow(10, std::log10(20) + i / 400.0 * 3)));
        QVERIFY(loudest + cut <= 0.001);
        QVERIFY(loudest + cut > -0.5);
    }

    void macrosMoveTheirBands() {
        const eq::Bands bass = eq::withMacro(eq::flat(), 0, 4);
        QVERIFY(std::abs(eq::macro(bass, 0) - 4) < 0.1);
        QCOMPARE(bass[9].gain, 0.0);
        QVERIFY(bass[0].gain > bass[2].gain);
    }

    void normalizedKeepsLimits() {
        eq::Bands b = eq::flat();
        b[0].gain = 40;
        b[1].freq = 5;
        b[2].q = 100;
        b = eq::normalized(b);
        QCOMPARE(b[0].gain, eq::MaxGain);
        QCOMPARE(b[1].freq, eq::MinFreq);
        QCOMPARE(b[2].q, eq::MaxQ);
        QCOMPARE(eq::normalized({}).size(), eq::BandCount);
    }

    void bandsSurviveJson() {
        const eq::Bands b = eq::fromGains({1, -2, 3, 0, 0, 0, 0, 0, 0, 4});
        QCOMPARE(eq::fromJson(eq::toJson(b)), b);
    }

    void parsesAutoEqProfiles() {
        const QString text = "Preamp: -6.1 dB\n"
                             "Filter 1: ON LSC Fc 105 Hz Gain 6.4 dB Q 0.70\n"
                             "Filter 2: ON PK Fc 8800 Hz Gain 5.1 dB Q 1.42\n"
                             "Filter 3: ON PK Fc 37 Hz Gain 0.7 dB Q 3.96\n"
                             "Filter 4: ON HSC Fc 10000 Hz Gain -2.1 dB Q 0.70\n";
        const eq::Bands b = eq::parseAutoEq(text);
        QCOMPARE(b.size(), eq::BandCount);
        for (int i = 1; i < b.size(); ++i)
            QVERIFY(b[i - 1].freq <= b[i].freq);
        // The four filters stay as given; six flat bands fill the gaps.
        int flat = 0;
        bool shelf = false, high = false;
        for (const eq::Band &band : b) {
            flat += band.gain == 0 && band.type == eq::FilterType::Peak;
            shelf |= band.type == eq::FilterType::LowShelf && band.freq == 105 && band.gain == 6.4;
            high |= band.type == eq::FilterType::HighShelf && band.freq == 10000 && band.gain == -2.1;
        }
        QCOMPARE(flat, 6);
        QVERIFY(shelf);
        QVERIFY(high);
        QVERIFY(eq::parseAutoEq("nothing here").isEmpty());
    }

    void readsTheAutoEqIndex() {
        const QString md = "# Index\n"
                           "- [Sennheiser HD 650](./oratory1990/over-ear/Sennheiser%20HD%20650) by oratory1990\n"
                           "- [Sennheiser HD 650](./crinacle/GRAS%2043AG-7%20over-ear/Sennheiser%20HD%20650) by crinacle\n"
                           "- [Bad](./../../etc/passwd) by nobody\n";
        const auto entries = AutoEq::parseIndex(md);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].path, QString("oratory1990/over-ear/Sennheiser HD 650"));
        QCOMPARE(entries[0].source, QString("oratory1990"));
    }

    void namesAutoEqPresets() {
        QCOMPARE(AutoEq::presetName("Sennheiser HD 650"), QString::fromUtf8("HD 650 · AutoEQ"));
        QCOMPARE(AutoEq::presetName("Apple AirPods Pro 2"), QString::fromUtf8("AirPods Pro 2 · AutoEQ"));
        QCOMPARE(AutoEq::presetName("Unknownbrand X1"), QString::fromUtf8("Unknownbrand X1 · AutoEQ"));
    }

    void processorPassesAudioThrough() {
        dsp::Processor p;
        const eq::Bands flat = eq::flat();
        p.post(dsp::makeParams(48000, true, flat, flat, 0, 1, dsp::Room::Off, 0));
        const auto in = sine(440, 0.5, 48000);
        const auto out = run(p, in);
        for (size_t i = 0; i < in.size(); ++i)
            QVERIFY(std::abs(out[i] - in[i]) < 1e-5);
    }

    void processorAppliesBandsAndPreamp() {
        dsp::Processor p;
        eq::Bands b = eq::flat();
        b[5].gain = 6; // 1 kHz
        const double preamp = eq::autoPreamp(b, b);
        p.post(dsp::makeParams(48000, true, b, b, preamp, 1, dsp::Room::Off, 0));
        const auto out = run(p, sine(1000, 0.25, 48000));
        // +6 dB at the centre, then the preamp's cut: close to unity.
        const double gainDb = 20 * std::log10(peakOf(out, 24000) / 0.25);
        QVERIFY2(std::abs(gainDb - (6 + preamp)) < 0.3, qPrintable(QString::number(gainDb)));
    }

    void bypassSkipsTheEqualizer() {
        dsp::Processor p;
        eq::Bands b = eq::flat();
        b[5].gain = 12;
        p.post(dsp::makeParams(48000, false, b, b, -12, 1, dsp::Room::Off, 0));
        const auto out = run(p, sine(1000, 0.25, 24000));
        QVERIFY(std::abs(peakOf(out, 12000) - 0.25) < 0.001);
    }

    void balanceTurnsOneSideDown() {
        dsp::Processor p;
        const eq::Bands flat = eq::flat();
        p.post(dsp::makeParams(48000, true, flat, flat, 0, 1, dsp::Room::Off, 50));
        // The gain ramps across the first block, so measure after it.
        const auto in = sine(440, 0.5, 9600);
        std::vector<float> outL(in.size()), outR(in.size());
        for (size_t at = 0; at < in.size(); at += 1024) {
            const uint32_t n = uint32_t(std::min<size_t>(1024, in.size() - at));
            const float *src[2] = {in.data() + at, in.data() + at};
            float *dst[2] = {outL.data() + at, outR.data() + at};
            p.process(src, dst, n);
        }
        QVERIFY(std::abs(peakOf(outL, 4800) - 0.25) < 0.01);
        QVERIFY(std::abs(peakOf(outR, 4800) - 0.5) < 0.01);
    }

    void effectControlsStayInTheirRange() {
        plugins::Param ratio;
        ratio.min = 1;
        ratio.max = 100;
        ratio.def = 4;
        QCOMPARE(plugins::clamp(ratio, 500), 100.f);
        QCOMPARE(plugins::clamp(ratio, 0), 1.f);
        QCOMPARE(plugins::clamp(ratio, 2.5f), 2.5f);
        QCOMPARE(plugins::clamp(ratio, NAN), 4.f);

        plugins::Param mode = ratio;
        mode.integer = true;
        QCOMPARE(plugins::clamp(mode, 2.6f), 3.f);

        plugins::Param toggle;
        toggle.toggled = true;
        QCOMPARE(plugins::clamp(toggle, 0.7f), 1.f);
        QCOMPARE(plugins::clamp(toggle, 0.2f), 0.f);

        plugins::Param open;
        open.min = -FLT_MAX;
        open.max = FLT_MAX;
        QCOMPARE(plugins::clamp(open, -1e6f), -1e6f);
    }

    void effectControlChangesTheSoundWhileItPlays() {
        const QString id = QStringLiteral("lv2:http://lsp-plug.in/plugins/lv2/compressor_stereo");
        if (!plugins::Catalog::shared().find(id, nullptr))
            QSKIP("lsp-plugins-lv2 is not installed");
        const QVector<plugins::Param> params = plugins::Catalog::shared().params(id, 48000);
        const auto gain = std::find_if(params.begin(), params.end(), [](const plugins::Param &p) { return p.symbol == "g_out"; });
        QVERIFY(gain != params.end());
        QCOMPARE(gain->def, 1.f);

        QString error;
        auto instance = plugins::Catalog::shared().instantiate(id, 48000, {}, &error);
        QVERIFY2(instance, qPrintable(error));
        // Far below the threshold, so the compressor only applies its gains.
        const auto in = sine(1000, 0.02, 48000);
        auto play = [&] {
            std::vector<float> l(in.size()), r(in.size());
            for (size_t at = 0; at < in.size(); at += 1024) {
                const uint32_t n = uint32_t(std::min<size_t>(1024, in.size() - at));
                const float *src[2] = {in.data() + at, in.data() + at};
                float *dst[2] = {l.data() + at, r.data() + at};
                instance->run(src, dst, n);
            }
            return peakOf(l, 24000);
        };
        const double before = play();
        instance->set(gain->port, 0.5f);
        const double after = play();
        QVERIFY2(std::abs(after / before - 0.5) < 0.02, qPrintable(QString("%1 -> %2").arg(before).arg(after)));
    }

    void analyzerFindsATone() {
        Analyzer a;
        const auto s = sine(1000, 0.5, Analyzer::Size);
        const QVector<float> levels = a.levels(s.data(), 48000);
        QCOMPARE(levels.size(), Analyzer::Bands);
        const int loudest = int(std::max_element(levels.begin(), levels.end()) - levels.begin());
        const double centre = 20 * std::pow(1000.0, (loudest + .5) / Analyzer::Bands);
        QVERIFY2(std::abs(std::log2(centre / 1000)) < 0.2, qPrintable(QString::number(centre)));
        QVERIFY(levels[loudest] > -8 && levels[loudest] < -4);
    }

    void themeFallsBackToTokyoNight() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.bg(), QColor("#1a1b26"));
        QCOMPARE(theme.ac(), QColor("#7aa2f7"));
    }

    void themeDerivesTokens() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml",
              "accent = \"#112233\"\nbackground = \"#000000\"\nforeground = \"#ffffff\"\n"
              "dark_background = \"#010101\"\nselection = \"#202020\"\ndark_foreground = \"#808080\"\ncyan = \"#00ffff\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.ac(), QColor("#112233"));
        QCOMPARE(theme.bd(), QColor("#010101"));
        QCOMPARE(theme.hl(), QColor("#202020"));
        QCOMPARE(theme.mu(), QColor("#808080"));
        QCOMPARE(theme.a2(), QColor("#00ffff"));
        // Lines sit a quarter of the way from a selected row to quiet text.
        QCOMPARE(theme.ln(), Theme::mix(QColor("#202020"), QColor("#808080"), .25));
    }

    void themeKeepsQuietTextReadable() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml",
              "background = \"#101010\"\nforeground = \"#f0f0f0\"\ndark_foreground = \"#141414\"\n");
        Theme theme(dir.path());
        QVERIFY(theme.mu() != QColor("#141414"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE_WITH_TIMEOUT(theme.radius(), 6, 5000);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "presets").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"presets": "predefinições", "bypass": "ignorar"})");
        write(dir.path() + "/pt-BR.json", R"({"presets": "predefinições BR"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "presets"), QString::fromUtf8("predefinições BR"));
        QCOMPARE(translator.translate("Main", "bypass"), QString("ignorar"));
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n streams": {"one": "%n stream", "other": "%n streams"}})");
        write(dir.path() + "/ru.json", R"({"%n streams": {"one": "%n поток", "few": "%n потока", "many": "%n потоков", "other": "%n потока"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n streams", nullptr, 21), QString::fromUtf8("%n поток"));
        QCOMPARE(translator.translate("Main", "%n streams", nullptr, 25), QString::fromUtf8("%n потоков"));
    }

    void shippedCatalogsParse() {
        // Every catalog compiled into the app must be valid and keep its
        // placeholders, or a language switch shows broken text.
        const QString root = QStringLiteral(PREAMP_SOURCE_DIR);
        for (const QString &dirName : {QStringLiteral("/src/i18n"), QStringLiteral("/src/shell/i18n")}) {
            QDir dir(root + dirName);
            for (const QString &name : dir.entryList({"*.json"})) {
                QFile file(dir.filePath(name));
                QVERIFY(file.open(QIODevice::ReadOnly));
                QJsonParseError error;
                const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
                QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(name + ": " + error.errorString()));
                const QJsonObject o = doc.object();
                for (auto it = o.begin(); it != o.end(); ++it) {
                    if (!it.value().isString())
                        continue;
                    for (const QString &ph : {QStringLiteral("%1"), QStringLiteral("%2")})
                        if (it.key().contains(ph))
                            QVERIFY2(it.value().toString().contains(ph), qPrintable(name + ": " + it.key()));
                }
            }
        }
    }

    void everyWindowStringIsTranslated() {
        // A string missing from a catalog stays in English after a language
        // switch, so every catalog must list every string the window shows.
        const QString root = QStringLiteral(PREAMP_SOURCE_DIR);
        // An ordinary string, as moc stops reading at a raw string with quotes.
        static const QRegularExpression call(QStringLiteral("qsTr\\(\"((?:[^\"\\\\]|\\\\.)*)\""));
        QSet<QString> strings;
        QDir qml(root + "/src/qml");
        for (const QString &name : qml.entryList({"*.qml"})) {
            QFile file(qml.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(file.readAll());
            for (auto it = call.globalMatch(text); it.hasNext();)
                strings.insert(it.next().captured(1).replace("\\\"", "\""));
        }
        QVERIFY(strings.size() > 50);
        QDir dir(root + "/src/i18n");
        for (const QString &name : dir.entryList({"*.json"})) {
            if (name == "en.json")
                continue;
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
            QStringList missing;
            for (const QString &s : std::as_const(strings))
                if (!o.contains(s))
                    missing << s;
            missing.sort();
            QVERIFY2(missing.isEmpty(), qPrintable(name + " lacks: " + missing.join(" | ")));
        }
    }

    void everySocketCommandHasACliCommand() {
        // Scripts and agents can do all that the window and the bar widget
        // can, so a new socket command needs a command line that sends it.
        const QString root = QStringLiteral(PREAMP_SOURCE_DIR);
        auto read = [&](const QString &path) {
            QFile file(root + path);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };
        const QString daemon = read("/src/daemon.cpp");
        const QString cli = read("/src/cli.cpp");
        static const QRegularExpression handled(QStringLiteral("cmd == \"([A-Za-z]+)\""));
        QSet<QString> commands;
        for (auto it = handled.globalMatch(daemon); it.hasNext();)
            commands.insert(it.next().captured(1));
        QVERIFY(commands.size() > 30);
        QStringList missing;
        for (const QString &c : std::as_const(commands))
            if (!cli.contains('"' + c + '"'))
                missing << c;
        missing.sort();
        QVERIFY2(missing.isEmpty(), qPrintable("no command line sends: " + missing.join(", ")));
    }

    void barWidgetLoadsOnOmarchy40() {
        // Omarchy 4.0.6 has no Commons.Translations, no Commons.I18n and no
        // lazy content in KeyboardPanel. A widget that uses one of them does
        // not load there at all.
        const QDir dir(QStringLiteral(PREAMP_SOURCE_DIR "/src/shell"));
        static const QRegularExpression comment(QStringLiteral("//[^\n]*"));
        static const QRegularExpression newer(QStringLiteral("Commons\\.(Translations|I18n)|^\\s*content:|loadedContent|ensureContent\\("),
                                              QRegularExpression::MultilineOption);
        QStringList found;
        for (const QString &name : dir.entryList({"*.qml", "*.js"})) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString source = QString::fromUtf8(file.readAll()).remove(comment);
            if (const QRegularExpressionMatch m = newer.match(source); m.hasMatch())
                found << name + ": " + m.captured(0).trimmed();
        }
        QVERIFY(dir.entryList({"*.qml"}).size() > 5);
        QVERIFY2(found.isEmpty(), qPrintable("needs Omarchy 4.5: " + found.join(", ")));
    }

    void miniCurveShowsThePresetShape() {
        // The preset card's thumbnail: Bass boost sits well above the zero
        // line at the low end and on it at the high end.
        GraphItem graph;
        graph.setSize(QSizeF(200, 60));
        graph.setLook("mini");
        graph.setProperty("accent", QColor(Qt::white));
        QVariantList bands;
        for (const eq::Band &b : eq::builtins().at(1).left)
            bands << QVariantMap{{"f", b.freq}, {"g", b.gain}, {"q", b.q}, {"t", eq::typeName(b.type)}};
        graph.setBands(bands);

        QImage image(200, 60, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);
        QPainter painter(&image);
        graph.paint(&painter);
        painter.end();

        // The brightest pixel in a column is the curve's stroke.
        auto strokeRow = [&](int x) {
            int row = -1, brightest = 0;
            for (int y = 0; y < image.height(); ++y)
                if (const int v = qGray(image.pixel(x, y)); v > brightest) {
                    brightest = v;
                    row = y;
                }
            return row;
        };
        QVERIFY2(strokeRow(4) < 18, qPrintable(QString::number(strokeRow(4))));
        QVERIFY2(std::abs(strokeRow(195) - 30) <= 1, qPrintable(QString::number(strokeRow(195))));
    }

    void iconFollowsThePalette() {
        const QString svg = brand::iconSvg(QColor("#16161e"), QColor("#3b4261"), QColor("#7aa2f7"), false);
        QVERIFY(svg.contains("fill=\"#7aa2f7\""));
        QVERIFY(svg.contains("x=\"16\" y=\"57\" width=\"28\" height=\"25\""));
        QVERIFY(svg.contains("x=\"56\" y=\"18\" width=\"28\" height=\"64\""));
        QVERIFY(!svg.contains("rx="));
        QVERIFY(brand::iconSvg(Qt::black, Qt::gray, Qt::white, true).contains("rx=\"22\""));
    }
};

QTEST_MAIN(PreampTests)
#include "preamp_tests.moc"
