#include "core/Text.h"

#include <algorithm>

namespace qaflow {

QString elideTitle(const QString& text, int max) {
    const QString clean = text.simplified();
    if (max <= 1) return clean.left(std::max(0, max));
    if (clean.size() <= max) return clean;
    QString head = clean.left(max - 1);
    // Por la última palabra entera, salvo que cortar por ella deje menos de la mitad del sitio.
    if (const int space = head.lastIndexOf(QLatin1Char(' ')); space >= max / 2) head.truncate(space);
    while (!head.isEmpty() && (head.back().isSpace() || head.back().isPunct())) head.chop(1);
    return head + QStringLiteral("…");
}

} // namespace qaflow
