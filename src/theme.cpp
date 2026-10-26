// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

namespace {

double luminance(const QColor &c) {
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
}

double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + .05) / (std::min(la, lb) + .05);
}

// The system monospace font, which Omarchy's font switcher sets through
// fontconfig. JetBrains Mono is Omarchy's default.
QString monospaceFamily() {
    QProcess fc;
    fc.start("fc-match", {"-f", "%{family[0]}", "monospace"});
    if (fc.waitForFinished(1000)) {
        const QString family = QString::fromUtf8(fc.readAllStandardOutput()).trimmed();
        if (!family.isEmpty())
            return family;
    }
    return "JetBrains Mono";
}

} // namespace

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory),
      m_font(directory.isEmpty() ? monospaceFamily() : QStringLiteral("JetBrains Mono")) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(2000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    m_palette = readPalette({});
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

QColor Theme::mix(const QColor &a, const QColor &b, double t) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

Theme::Palette Theme::readPalette(const QString &path) {
    QHash<QString, QColor> keys;
    QFile file(path);
    if (!path.isEmpty() && file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression line(R"re(^\s*([a-z_]+)\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                             QRegularExpression::MultilineOption);
        auto it = line.globalMatch(QString::fromUtf8(file.readAll()));
        while (it.hasNext()) {
            const auto m = it.next();
            keys.insert(m.captured(1), QColor(m.captured(2)));
        }
    }
    auto key = [&](const char *name, const char *fallback) {
        return keys.value(QString::fromLatin1(name), QColor(fallback));
    };

    Palette p;
    p.bg = key("background", "#1a1b26");
    p.fg = key("foreground", "#c0caf5");
    p.bd = keys.contains("dark_background") ? keys.value("dark_background") : mix(p.bg, QColor("black"), .18);
    p.hl = keys.contains("selection") ? keys.value("selection")
         : keys.contains("lighter_background") ? keys.value("lighter_background") : mix(p.bg, p.fg, .1);
    p.mu = keys.contains("dark_foreground") ? keys.value("dark_foreground") : mix(p.fg, p.bg, .42);
    // Quiet text still has to read: a theme whose dimmed color is too close
    // to its background gets one mixed from the text color instead.
    if (contrast(p.mu, p.bg) < 2.4)
        p.mu = mix(p.fg, p.bg, .42);
    // Lines sit between a selected row and quiet text.
    p.ln = mix(p.hl, p.mu, .25);
    p.ac = key("accent", "#7aa2f7");
    p.a2 = keys.value("cyan", keys.value("blue", QColor("#7dcfff")));
    p.gn = key("green", "#9ece6a");
    p.yl = key("yellow", "#e0af68");
    p.rd = key("red", "#f7768e");
    return p;
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const Palette next = readPalette(m_directory + "/theme/colors.toml");
    const bool same = next.bg == m_palette.bg && next.bd == m_palette.bd && next.hl == m_palette.hl &&
                      next.ln == m_palette.ln && next.fg == m_palette.fg && next.mu == m_palette.mu &&
                      next.ac == m_palette.ac && next.a2 == m_palette.a2 && next.gn == m_palette.gn &&
                      next.yl == m_palette.yl && next.rd == m_palette.rd;
    if (!same) {
        m_palette = next;
        emit changed();
    }
}
