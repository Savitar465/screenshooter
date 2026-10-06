#include "Update.h"

#include <QRegularExpression>
#include <QStringList>

namespace qaflow {

std::optional<Version> Version::parse(const QString& text) {
    static const QRegularExpression re(QStringLiteral(R"(^[vV]?(\d+)\.(\d+)(?:\.(\d+))?(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$)"));
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch()) return std::nullopt;
    Version v;
    v.major = m.captured(1).toInt();
    v.minor = m.captured(2).toInt();
    v.patch = m.captured(3).toInt();   // "1.6" es 1.6.0
    v.preRelease = m.captured(4);
    return v;
}

QString Version::toString() const {
    QString s = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    if (isPreRelease()) s += QLatin1Char('-') + preRelease;
    return s;
}

int Version::compare(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    // Una versión previa va antes que la final del mismo número.
    if (a.preRelease.isEmpty() || b.preRelease.isEmpty()) {
        if (a.preRelease.isEmpty() == b.preRelease.isEmpty()) return 0;
        return a.preRelease.isEmpty() ? 1 : -1;
    }
    // Entre previas, campo a campo: los numéricos por valor y antes que los de texto; si todos coinciden,
    // la de más campos va después ("beta" < "beta.1").
    const QStringList pa = a.preRelease.split(QLatin1Char('.'));
    const QStringList pb = b.preRelease.split(QLatin1Char('.'));
    for (int i = 0; i < std::min(pa.size(), pb.size()); ++i) {
        bool na = false, nb = false;
        const qulonglong ia = pa[i].toULongLong(&na);
        const qulonglong ib = pb[i].toULongLong(&nb);
        if (na && nb) {
            if (ia != ib) return ia < ib ? -1 : 1;
        } else if (na != nb) {
            return na ? -1 : 1;
        } else if (const int c = QString::compare(pa[i], pb[i]); c != 0) {
            return c < 0 ? -1 : 1;
        }
    }
    if (pa.size() != pb.size()) return pa.size() < pb.size() ? -1 : 1;
    return 0;
}

QString toString(UpdateChannel c) {
    return c == UpdateChannel::Beta ? QStringLiteral("beta") : QStringLiteral("stable");
}

UpdateChannel updateChannelFromString(const QString& s) {
    return s == QLatin1String("beta") ? UpdateChannel::Beta : UpdateChannel::Stable;
}

const UpdateAsset* UpdateRelease::asset(const QString& name) const {
    for (const UpdateAsset& a : assets)
        if (a.name == name) return &a;
    return nullptr;
}

QHash<QString, QString> parseChecksums(const QByteArray& text) {
    static const QRegularExpression re(QStringLiteral(R"(^([0-9A-Fa-f]{64}) [ *](.+)$)"));
    QHash<QString, QString> out;
    for (const QByteArray& raw : text.split('\n')) {
        const auto m = re.match(QString::fromUtf8(raw).trimmed());
        if (!m.hasMatch()) continue;
        QString name = m.captured(2).trimmed();
        if (name.startsWith(QLatin1Char('*'))) name.remove(0, 1);
        out.insert(name, m.captured(1).toLower());
    }
    return out;
}

std::optional<UpdateRelease> newestUpdate(const QList<UpdateRelease>& releases, const Version& current, UpdateChannel channel) {
    std::optional<UpdateRelease> best;
    for (const UpdateRelease& r : releases) {
        if (r.version.isPreRelease() && channel != UpdateChannel::Beta) continue;
        if (!(current < r.version)) continue;
        if (!best || best->version < r.version) best = r;
    }
    return best;
}

} // namespace qaflow
