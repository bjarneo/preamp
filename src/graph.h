#pragma once

// Draws the frequency response the way the design's SVG does: log frequency
// across, ±15 dB up and down, the curve sampled at fixed points. Three looks:
//
//   studio  analyzer bars, grid, selected band and total fills, other channel
//   param   grid, every band's own curve, a heavier total curve
//   bars    forty bars around the centre line, other channel dashed
//   mini    the curve alone, for the preset cards

#include <QColor>
#include <QQuickPaintedItem>
#include <QVariantList>

#include "eq.h"

class GraphItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString look READ look WRITE setLook NOTIFY changed)
    Q_PROPERTY(QVariantList bands READ bands WRITE setBands NOTIFY changed)
    Q_PROPERTY(QVariantList otherBands READ otherBands WRITE setOtherBands NOTIFY changed)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY changed)
    Q_PROPERTY(bool bypass READ bypass WRITE setBypass NOTIFY changed)
    Q_PROPERTY(QVariantList spectrum READ spectrum WRITE setSpectrum NOTIFY changed)
    Q_PROPERTY(bool showSpectrum READ showSpectrum WRITE setShowSpectrum NOTIFY changed)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY changed)
    Q_PROPERTY(QColor accent2 MEMBER m_accent2 NOTIFY changed)
    Q_PROPERTY(QColor muted MEMBER m_muted NOTIFY changed)
    Q_PROPERTY(QColor line MEMBER m_line NOTIFY changed)
    Q_PROPERTY(QColor text MEMBER m_text NOTIFY changed)

public:
    explicit GraphItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    QString look() const { return m_look; }
    void setLook(const QString &look);
    QVariantList bands() const { return m_bandsVar; }
    void setBands(const QVariantList &bands);
    QVariantList otherBands() const { return m_otherVar; }
    void setOtherBands(const QVariantList &bands);
    int selected() const { return m_selected; }
    void setSelected(int selected);
    bool bypass() const { return m_bypass; }
    void setBypass(bool bypass);
    QVariantList spectrum() const { return m_spectrumVar; }
    void setSpectrum(const QVariantList &levels);
    bool showSpectrum() const { return m_showSpectrum; }
    void setShowSpectrum(bool show);

signals:
    void changed();

private:
    static eq::Bands toBands(const QVariantList &list);

    QString m_look = "studio";
    QVariantList m_bandsVar, m_otherVar, m_spectrumVar;
    eq::Bands m_bands, m_other;
    QVector<float> m_spectrum;
    int m_selected = 0;
    bool m_bypass = false;
    bool m_showSpectrum = true;
    QColor m_accent{"#7aa2f7"}, m_accent2{"#7dcfff"}, m_muted{"#737aa2"}, m_line{"#3b4261"}, m_text{"#c0caf5"};
};
