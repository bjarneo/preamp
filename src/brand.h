#pragma once

// The preamp mark: a square tile with a quiet input block and a louder
// output block on a shared baseline. Input 28% wide and 25% tall at 16%,
// output 28% wide and 64% tall at 56%, both ending at 82%.

#include <QColor>
#include <QString>

namespace brand {

QString iconSvg(const QColor &tile, const QColor &input, const QColor &output, bool rounded);

// Writes the mark in the current theme's colors where icon lookups find it
// before the packaged one, so the launcher follows the theme. Returns false
// when nothing changed.
bool writeThemedIcon(const QColor &tile, const QColor &input, const QColor &output, bool rounded);

} // namespace brand
