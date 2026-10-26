#pragma once

// Follows the Omarchy theme live and hands QML the design's color tokens:
//
//   bg  window          bd  darker panels     hl  selected rows
//   ln  lines           fg  text              mu  quiet text
//   ac  accent          a2  second accent     gn yl rd  meters and warnings
//
// It also tracks Hyprland's window rounding as `radius` and the system
// monospace font as `font`.

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor bg READ bg NOTIFY changed)
    Q_PROPERTY(QColor bd READ bd NOTIFY changed)
    Q_PROPERTY(QColor hl READ hl NOTIFY changed)
    Q_PROPERTY(QColor ln READ ln NOTIFY changed)
    Q_PROPERTY(QColor fg READ fg NOTIFY changed)
    Q_PROPERTY(QColor mu READ mu NOTIFY changed)
    Q_PROPERTY(QColor ac READ ac NOTIFY changed)
    Q_PROPERTY(QColor a2 READ a2 NOTIFY changed)
    Q_PROPERTY(QColor gn READ gn NOTIFY changed)
    Q_PROPERTY(QColor yl READ yl NOTIFY changed)
    Q_PROPERTY(QColor rd READ rd NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(QString font READ font CONSTANT)

public:
    struct Palette {
        QColor bg, bd, hl, ln, fg, mu, ac, a2, gn, yl, rd;
    };

    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QColor bg() const { return m_palette.bg; }
    QColor bd() const { return m_palette.bd; }
    QColor hl() const { return m_palette.hl; }
    QColor ln() const { return m_palette.ln; }
    QColor fg() const { return m_palette.fg; }
    QColor mu() const { return m_palette.mu; }
    QColor ac() const { return m_palette.ac; }
    QColor a2() const { return m_palette.a2; }
    QColor gn() const { return m_palette.gn; }
    QColor yl() const { return m_palette.yl; }
    QColor rd() const { return m_palette.rd; }
    int radius() const { return m_radius; }
    QString font() const { return m_font; }
    // The service reads the rounding again only when the theme changes. It
    // has no window to keep in step.
    void stopRoundingPoll() { m_roundingPoll.stop(); }
    const Palette &palette() const { return m_palette; }

    // Tokens from a colors.toml. Missing keys fall back to Tokyo Night.
    static Palette readPalette(const QString &path);
    static QColor mix(const QColor &a, const QColor &b, double t);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    Palette m_palette;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QString m_font;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
