#include "QSettingsUpdatePreferences.h"

#include <QSettings>

namespace qaflow {

UpdatePreferences QSettingsUpdatePreferences::load() {
    QSettings s;
    s.beginGroup(QStringLiteral("updates"));
    UpdatePreferences p;
    p.autoCheck = s.value(QStringLiteral("autoCheck"), p.autoCheck).toBool();
    p.channel = updateChannelFromString(s.value(QStringLiteral("channel"), toString(p.channel)).toString());
    p.skippedVersion = s.value(QStringLiteral("skippedVersion")).toString();
    p.lastCheck = QDateTime::fromString(s.value(QStringLiteral("lastCheck")).toString(), Qt::ISODate);
    return p;
}

void QSettingsUpdatePreferences::save(const UpdatePreferences& p) {
    QSettings s;
    s.beginGroup(QStringLiteral("updates"));
    s.setValue(QStringLiteral("autoCheck"), p.autoCheck);
    s.setValue(QStringLiteral("channel"), toString(p.channel));
    s.setValue(QStringLiteral("skippedVersion"), p.skippedVersion);
    s.setValue(QStringLiteral("lastCheck"), p.lastCheck.toUTC().toString(Qt::ISODate));
}

} // namespace qaflow
