#include "graph.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {

const double LogMin = std::log10(20.0);
const double LogMax = std::log10(20000.0);

double logX(double f) { return (std::log10(f) - LogMin) / (LogMax - LogMin); }

// Rounded to a tenth, as the design writes its path data.
double tenth(double v) { return std::round(v * 10) / 10; }

// `span` is the decibels from the bottom edge to the top edge.
template <typename Fn>
QPainterPath curve(Fn fn, double w, double h, int n, double span = 30) {
    QPainterPath path;
    for (int i = 0; i <= n; ++i) {
        const double t = double(i) / n;
        const double f = std::pow(10, LogMin + t * (LogMax - LogMin));
        const double y = std::clamp(h / 2 - fn(f) * h / span, -4.0, h + 4);
        const QPointF p(tenth(t * w), tenth(y));
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    return path;
}

QPainterPath closed(QPainterPath path, double w, double h) {
    path.lineTo(w, h / 2);
    path.lineTo(0, h / 2);
    path.closeSubpath();
    return path;
}

QPainterPath grid(double w, double h) {
    QPainterPath path;
    for (double f : {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000}) {
        const double x = tenth(logX(f) * w);
        path.moveTo(x, 0);
        path.lineTo(x, h);
    }
    for (double g : {12, 6, -6, -12}) {
        const double y = tenth(h / 2 - g * h / 30);
        path.moveTo(0, y);
        path.lineTo(w, y);
    }
    return path;
}

// SVG strokes: butt caps, miter joins, dashes in pixels.
QPen svgPen(const QColor &color, double width, const QVector<double> &dashes = {}) {
    QPen pen(color, width);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    pen.setMiterLimit(4);
    if (!dashes.isEmpty()) {
        QVector<qreal> pattern;
        for (double d : dashes)
            pattern << d / width;
        pen.setDashPattern(pattern);
    }
    return pen;
}

} // namespace

GraphItem::GraphItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &GraphItem::changed, this, [this] { update(); });
}

eq::Bands GraphItem::toBands(const QVariantList &list) {
    eq::Bands bands;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        eq::Band b;
        b.freq = m.value("f").toDouble();
        b.gain = m.value("g").toDouble();
        b.q = m.value("q").toDouble();
        b.type = eq::typeFromName(m.value("t").toString());
        if (b.freq > 0 && b.q > 0)
            bands << b;
    }
    return bands;
}

void GraphItem::setLook(const QString &look) {
    if (look == m_look)
        return;
    m_look = look;
    emit changed();
}

void GraphItem::setBands(const QVariantList &bands) {
    m_bandsVar = bands;
    m_bands = toBands(bands);
    emit changed();
}

void GraphItem::setOtherBands(const QVariantList &bands) {
    m_otherVar = bands;
    m_other = toBands(bands);
    emit changed();
}

void GraphItem::setSelected(int selected) {
    if (selected == m_selected)
        return;
    m_selected = selected;
    emit changed();
}

void GraphItem::setBypass(bool bypass) {
    if (bypass == m_bypass)
        return;
    m_bypass = bypass;
    emit changed();
}

void GraphItem::setSpectrum(const QVariantList &levels) {
    m_spectrumVar = levels;
    m_spectrum.resize(levels.size());
    for (int i = 0; i < levels.size(); ++i)
        m_spectrum[i] = levels[i].toFloat();
    if (m_showSpectrum)
        update();
}

void GraphItem::setShowSpectrum(bool show) {
    if (show == m_showSpectrum)
        return;
    m_showSpectrum = show;
    emit changed();
}

void GraphItem::paint(QPainter *p) {
    const double w = width(), h = height();
    if (w <= 0 || h <= 0)
        return;
    p->setRenderHint(QPainter::Antialiasing, true);
    const QColor stroke = m_bypass ? m_muted : m_accent;
    const QVector<double> bypassDash = m_bypass ? QVector<double>{4, 4} : QVector<double>{};
    auto total = [this](double f) { return eq::response(m_bands, f); };
    auto other = [this](double f) { return eq::response(m_other, f); };

    // A preset's shape in a small space: the zero line, a light fill and the
    // curve, over ±10 dB so gentle presets still show their shape.
    if (m_look == "mini") {
        p->setRenderHint(QPainter::Antialiasing, false);
        p->fillRect(QRectF(0, std::round(h / 2), w, 1), m_line);
        p->setRenderHint(QPainter::Antialiasing, true);
        const QPainterPath path = curve(total, w, h, 120, 20);
        QColor fill = stroke;
        fill.setAlphaF(.14f);
        p->fillPath(closed(path, w, h), fill);
        p->strokePath(path, svgPen(stroke, 1.5));
        return;
    }

    if (m_look == "bars") {
        p->setRenderHint(QPainter::Antialiasing, false);
        p->fillRect(QRectF(0, std::round(h / 2), w, 1), m_line);
        for (int i = 0; i < 40; ++i) {
            const double f = std::pow(10, LogMin + (i + .5) / 40 * (LogMax - LogMin));
            const double g = eq::response(m_bands, f);
            const double bh = std::max(1.0, std::abs(g) * 4);
            const double top = g >= 0 ? h / 2 - bh : h / 2;
            const QColor c = m_bypass ? m_muted : g >= 0 ? m_accent : m_accent2;
            const double y0 = std::round(top), y1 = std::round(top + bh);
            p->fillRect(QRectF(2 + i * 6, y0, 4, std::max(1.0, y1 - y0)), c);
        }
        if (!m_other.isEmpty()) {
            p->setRenderHint(QPainter::Antialiasing, true);
            p->strokePath(curve(other, w, h, 80), svgPen(m_text, 1.5, {3, 3}));
        }
        return;
    }

    const bool studio = m_look == "studio";
    if (studio && m_showSpectrum && !m_spectrum.isEmpty()) {
        const int n = int(m_spectrum.size());
        const double bw = w / n;
        QColor body = m_accent2, cap = m_accent2;
        body.setAlphaF(.16f);
        cap.setAlphaF(.6f);
        for (int i = 0; i < n; ++i) {
            const double bh = std::clamp((m_spectrum[i] + 72.0) / 72.0, 0.0, 1.0) * h;
            if (bh <= 0)
                continue;
            p->fillRect(QRectF(i * bw + 1, h - bh, bw - 2, bh), body);
            p->fillRect(QRectF(i * bw + 1, h - bh, bw - 2, 2), cap);
        }
    }

    p->setOpacity(studio ? .7 : .6);
    p->strokePath(grid(w, h), svgPen(m_line, 1));
    p->setOpacity(studio ? .8 : 1);
    p->strokePath([&] {
        QPainterPath zero;
        zero.moveTo(0, h / 2);
        zero.lineTo(w, h / 2);
        return zero;
    }(), svgPen(m_muted, 1));
    p->setOpacity(1);

    if (studio) {
        const QPainterPath totalPath = curve(total, w, h, 240);
        if (m_selected >= 0 && m_selected < m_bands.size()) {
            const eq::Band band = m_bands[m_selected];
            QColor sel = m_accent2;
            sel.setAlphaF(.22f);
            p->fillPath(closed(curve([&](double f) { return eq::response(band, f); }, w, h, 160), w, h), sel);
        }
        QColor fill = m_accent;
        fill.setAlphaF(.1f);
        p->fillPath(closed(totalPath, w, h), fill);
        if (!m_other.isEmpty())
            p->strokePath(curve(other, w, h, 200), svgPen(m_accent2, 1.5, {5, 4}));
        p->strokePath(totalPath, svgPen(stroke, 2, bypassDash));
        return;
    }

    // Parametric: every band's own curve, one path so crossings do not darken.
    QPainterPath each;
    for (const eq::Band &band : std::as_const(m_bands))
        each.addPath(curve([&](double f) { return eq::response(band, f); }, w, h, 90));
    p->setOpacity(.6);
    p->strokePath(each, svgPen(m_muted, 1));
    p->setOpacity(1);
    p->strokePath(curve(total, w, h, 300), svgPen(stroke, 3, bypassDash));
}
