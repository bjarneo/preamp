#include "brand.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

namespace brand {

QString iconSvg(const QColor &tile, const QColor &input, const QColor &output, bool rounded) {
    const QString radius = rounded ? QStringLiteral(" rx=\"22\"") : QString();
    return QStringLiteral(
               "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\" width=\"256\" height=\"256\">\n"
               "  <rect width=\"100\" height=\"100\"%1 fill=\"%2\"/>\n"
               "  <rect x=\"16\" y=\"57\" width=\"28\" height=\"25\" fill=\"%3\"/>\n"
               "  <rect x=\"56\" y=\"18\" width=\"28\" height=\"64\" fill=\"%4\"/>\n"
               "</svg>\n")
        .arg(radius, tile.name(), input.name(), output.name());
}

bool writeThemedIcon(const QColor &tile, const QColor &input, const QColor &output, bool rounded) {
    const QString dir = QDir::homePath() + "/.local/share/icons/hicolor/scalable/apps";
    const QString path = dir + "/preamp.svg";
    const QByteArray svg = iconSvg(tile, input, output, rounded).toUtf8();
    QFile current(path);
    if (current.open(QIODevice::ReadOnly) && current.readAll() == svg)
        return false;
    current.close();
    QDir().mkpath(dir);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(svg);
    return file.commit();
}

} // namespace brand
